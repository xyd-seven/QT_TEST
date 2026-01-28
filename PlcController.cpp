#include "PlcController.h"
#include <QDebug>

PlcController::PlcController(QObject *parent) : QObject(parent)
{
    m_isEnabled = false;
    m_ip = "";
    m_port = 0;
    m_lastStartSignalVal = -1;
    m_pollStep = 0;
    m_ignoreStopSignalUntil = 0; // [新增] 初始化

    m_socket = new QTcpSocket(this);
    m_pollTimer = new QTimer(this);

    // [新增] 初始化写入队列定时器，间隔 50ms 防止粘包
    m_writeTimer = new QTimer(this);
    m_writeTimer->setInterval(50);
    connect(m_writeTimer, &QTimer::timeout, this, &PlcController::processWriteQueue);

    // 连接 Socket 信号
    connect(m_socket, &QTcpSocket::connected, this, &PlcController::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &PlcController::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &PlcController::onSocketReadyRead);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &PlcController::onSocketError);

    // 200ms 轮询一次 (交替读取实际上是 400ms 一个全周期)
    connect(m_pollTimer, &QTimer::timeout, this, &PlcController::onPollTimerTimeout);
}

PlcController::~PlcController()
{
    disconnectPlc();
}

void PlcController::init(bool enabled, const QString &ip, int port)
{
    m_isEnabled = enabled;
    m_ip = ip;
    m_port = port;

    if (m_isEnabled) {
        // 如果已经连接或正在连接，先断开
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->abort();
        }
        emit logMessage(QString("正在连接 PLC (%1:%2)...").arg(m_ip).arg(m_port));
        m_socket->connectToHost(m_ip, m_port);
    } else {
        disconnectPlc();
    }
}

void PlcController::disconnectPlc()
{
    m_pollTimer->stop();
    if (m_socket->isOpen()) {
        m_socket->disconnectFromHost();
        m_socket->close();
    }
    emit plcDisconnected();
}

void PlcController::writeDevice(int address, bool value)
{
    if (!m_isEnabled) return;

    // [关键逻辑] 如果是写入 M1650=1 (测试完成)，则在未来 3秒内忽略 M1650 的读取
    // 防止 PLC 还没来得及复位，我们自己读回来导致误判为“强制停止”
    if (address == 1650 && value) {
        m_ignoreStopSignalUntil = QDateTime::currentMSecsSinceEpoch() + 3000;
        qDebug() << "Writing M1650=1, ignoring Stop Signal for 3 seconds.";
    }

    // 加入队列
    WriteTask task;
    task.address = address;
    task.value = value;
    m_writeQueue.enqueue(task);

    // 如果定时器没跑，就启动它
    if (!m_writeTimer->isActive()) {
        m_writeTimer->start();
    }
}

// -----------------------------------------------------------
// 3. [新增] 队列处理函数
// -----------------------------------------------------------
void PlcController::processWriteQueue()
{
    if (m_writeQueue.isEmpty()) {
        m_writeTimer->stop();
        return;
    }

    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        WriteTask task = m_writeQueue.dequeue();
        QByteArray packet = buildWritePacket(task.address, task.value);
        m_socket->write(packet);
        // flush 确保立即发送，虽然 Qt socket 也是异步的，但这样更保险
        m_socket->flush();
    }
}

void PlcController::onSocketConnected()
{
    emit connected();
    emit logMessage("PLC 连接成功");
    // 连接成功后启动心跳轮询
    if (!m_pollTimer->isActive()) {
        m_pollTimer->start(200);
    }
}

void PlcController::onSocketDisconnected()
{
    m_pollTimer->stop();
    emit plcDisconnected();
    emit logMessage("PLC 连接断开");

    // 自动重连机制: 3秒后尝试重连
    if (m_isEnabled) {
        QTimer::singleShot(3000, this, [this](){
            if (m_isEnabled && m_socket->state() == QAbstractSocket::UnconnectedState) {
                m_socket->connectToHost(m_ip, m_port);
            }
        });
    }
}

void PlcController::onSocketError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);
    // 生产环境只保留简单的错误日志
    // emit logMessage(QString("PLC 通信错误: %1").arg(m_socket->errorString()));
}

void PlcController::onSocketReadyRead()
{
    QByteArray data = m_socket->readAll();
    parseResponse(data);
}

void PlcController::onPollTimerTimeout()
{
    if (!m_isEnabled || !m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    int address = 1600; // 固定为启动信号地址

    // 发送读指令 (读 1 位)
    QByteArray cmd = buildReadPacket(address, 1);
    m_socket->write(cmd);
    m_socket->flush();
}

QByteArray PlcController::buildReadPacket(int address, int count)
{
    Q_UNUSED(count);
    int timeout = 10;
    // 保持标准 ASCII 协议格式: 00FF + 超时 + ID(4D2) + ... + 地址
    QString cmdStr = QString::asprintf("00FF%04X4D200000%04X0100", timeout, address);
    return cmdStr.toLatin1();
}

QByteArray PlcController::buildWritePacket(int address, bool value)
{
    int timeout = 10;
    QString cmdStr = QString::asprintf("02FF%04X4D200000%04X0100%01X",
                                       timeout, address, value ? 1 : 0);
    return cmdStr.toLatin1();
}

void PlcController::parseResponse(const QByteArray &data)
{
    QString resp = QString::fromLatin1(data).trimmed();

    // 读指令的响应通常是 "80000" 或 "80001" (A-1E 协议)
    // 写指令的响应是 "8000"
    if (resp.startsWith("8000")) {

        // 如果长度只有 4 (即 "8000")，说明是写的响应，或者是无效读取，直接忽略
        if (resp.length() <= 4) return;

        bool isOn = resp.endsWith("1");

        // --- 【修改后】 直接视为 M1600 (Start Signal) 的逻辑 ---

        if (isOn) {
            // M1600 = 1
            if (m_lastStartSignalVal != 1) {
                m_lastStartSignalVal = 1;
                qDebug() << "PLC Start Signal (M1600) Rising Edge Detected!";
                emit logMessage(">>> [PLC] 收到启动信号 (M1600=1)");
                emit plcStartSignalReceived(); // 触发主窗口开始测试
            }
        }
        else {
            // M1600 = 0
            if (m_lastStartSignalVal != 0) {
                m_lastStartSignalVal = 0;
                // 归零，等待下一次上升沿
                // qDebug() << "PLC Start Signal Reset (M1600=0)";
            }
        }
    }
}
