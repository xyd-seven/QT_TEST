// 文件: PlcController.h

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QMutex>
#include <QQueue>     // [新增]
#include <QDateTime>  // [新增]

// [新增] 写入指令结构体
struct WriteTask {
    int address;
    bool value;
};

class PlcController : public QObject
{
    Q_OBJECT

public:
    explicit PlcController(QObject *parent = nullptr);
    ~PlcController();

    void init(bool enabled, const QString &ip, int port);
    void disconnectPlc();
    void writeDevice(int address, bool value);

signals:
    void logMessage(const QString &msg);
    void plcStartSignalReceived();
    void plcStopSignalReceived();
    void connected();
    void plcDisconnected();
    void errorOccurred(const QString &msg);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError socketError);
    void onPollTimerTimeout();

    // [新增] 队列处理槽函数
    void processWriteQueue();

private:
    QByteArray buildReadPacket(int address, int count);
    QByteArray buildWritePacket(int address, bool value);
    void parseResponse(const QByteArray &data);

private:
    bool m_isEnabled;
    QString m_ip;
    int m_port;

    QTcpSocket *m_socket;
    QTimer *m_pollTimer;
    int m_pollStep;             // 0=读Stop(M1650), 1=读Start(M1600)
    int m_lastStartSignalVal;   // 边沿检测用

    // [新增] 写入队列相关
    QQueue<WriteTask> m_writeQueue;
    QTimer *m_writeTimer;       // 发送间隔定时器

    // [新增] 忽略停止信号的时间戳
    qint64 m_ignoreStopSignalUntil;
};
