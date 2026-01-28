#include "MainWindow.h"
#include <cmath> //用于计算平方根 ceil/sqrt
#include <QtGlobal>
#include <QMessageBox>
#include <QApplication>  // <--- 解决 'qApp' was not declared
#include <QKeyEvent>     // <--- 解决后面 eventFilter 里用到的 QKeyEvent
#include <QRegularExpression> // <--- 必须添加这个
#include <QRegularExpression>   // 引入正则头文件
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QDebug>
#include <QTextCodec>
#include <QThread>
#include "ConfigManager.h" // 必须包含

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_lblCacheStatus(nullptr) // 【修复 1】 必须初始化指针为空，防止野指针崩溃
{
    // 1. 设置主窗口基本属性
    resize(1280, 768);
    setWindowTitle("多通道产线并行测试系统 (Dynamic Grid)");

    // 2. 初始化中心部件和网格布局
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);

    m_gridLayout = new QGridLayout(m_centralWidget);
    m_gridLayout->setContentsMargins(4, 4, 4, 4);
    m_gridLayout->setSpacing(4);

    // 【新增】 安装全局事件过滤器
    // 这句话的意思是：整个应用程序(qApp)所有的事件，先发给我(this)过目一遍！
    qApp->installEventFilter(this);

    // 3. 创建顶部工具栏
    QToolBar *toolbar = addToolBar("Settings");
    toolbar->setMovable(false);

    // --- [通道设置部分] ---
    QLabel *lblSet = new QLabel("当前检测通道数: ", this);
    lblSet->setFont(QFont("Microsoft YaHei", 10, QFont::Bold));
    toolbar->addWidget(lblSet);

    // [设置控件] 数字微调框
    m_spinBoxCount = new QSpinBox(this);
    m_spinBoxCount->setRange(1, 9);
    m_spinBoxCount->setValue(4);
    m_spinBoxCount->setFont(QFont("Arial", 10));
    m_spinBoxCount->setMinimumWidth(60);
    toolbar->addWidget(m_spinBoxCount);

    connect(m_spinBoxCount, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onChannelCountChanged);


    // --- [PLC 状态指示灯] ---
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    toolbar->addWidget(spacer);

    // =======================================================
    // 【修复 2】 必须在这里实例化 m_lblCacheStatus
    // 之前缺失这段代码，导致 PLC 触发清空时操作空指针崩溃
    // =======================================================
    m_lblCacheStatus = new QLabel("待匹配: (空)", this);
    m_lblCacheStatus->setStyleSheet("color: blue; font-weight: bold; margin-right: 10px;");
    m_lblCacheStatus->setMinimumWidth(200); // 给够宽度，防止文字跳动
    toolbar->addWidget(m_lblCacheStatus);

    // 加个分割线
    toolbar->addSeparator();
    // =======================================================

    m_lblPlcStatus = new QLabel(this);
    m_lblPlcStatus->setMinimumWidth(130);
    m_lblPlcStatus->setAlignment(Qt::AlignCenter);
    toolbar->addWidget(m_lblPlcStatus);
    updatePlcStatusIndicator(0); // 默认灰色

    // =======================================================
    // 4. [新增] 优先加载配置文件 (最关键的一步！)
    // =======================================================

    // 自动扫描 configs 文件夹下的 .json 文件
    QStringList configFiles = ConfigManager::getConfigFileList();

    if (!configFiles.isEmpty()) {
        // 策略：默认加载找到的【第一个】文件
        QString autoFile = configFiles.first();
        qDebug() << ">>> [System] Auto-loading config file:" << autoFile;

        // 这行代码执行后，ConfigManager 内存里就有数据了
        ConfigManager::instance().loadConfig(autoFile);
    }
    else {
        qWarning() << ">>> [Warning] No config files found in 'configs/'. Using defaults.";
    }

    // =======================================================
    // 5. 初始化业务管理器
    // =======================================================

    // --- [SN 管理器] ---
    // 注意：DeviceChannelWidget 内部有自己的 SnManager，
    // 这里保留它是为了 MainWindow 可能需要的全局校验或文件读取。
    m_snManager = new SnManager(this);
    QString snPath = "configs/sn_data.csv";

    if(m_snManager->loadData(snPath)) {
        qDebug() << "SN Data loaded successfully.";
    } else {
        qDebug() << "Warning: SN Data load failed or file missing.";
    }

    // --- [PLC 控制器] ---
    m_plc = new PlcController(this);

    // ============================================================
    // 【修复 3 - 核心】 连接调试日志信号
    // 没有这行，PlcController 里的 emit logMessage(...) 就没地方显示
    // ============================================================
    connect(m_plc, &PlcController::logMessage, this, &MainWindow::appendToLog);

    // 连接连接状态信号
    connect(m_plc, &PlcController::connected, this, [this](){
        updatePlcStatusIndicator(2); // 绿色
    });
    connect(m_plc, &PlcController::plcDisconnected, this, [this](){
        updatePlcStatusIndicator(1); // 红色
    });

    // 连接业务逻辑信号 (Start)
    connect(m_plc, &PlcController::plcStartSignalReceived, this, &MainWindow::onPlcStartSignal);

    // ============================================================
    // 【修复 4】 补上停止信号的连接 (M1650 Stop)
    // ============================================================
    connect(m_plc, &PlcController::plcStopSignalReceived, this, &MainWindow::onPlcStopSignal);

    // --- [读取 PLC 配置并启动] ---
    // 此时 ConfigManager 已经加载了文件，这里能读到真正的配置了
    PlcConfig plcConf = ConfigManager::instance().getPlcConfig();

    // 打印调试信息，确保读到了配置
    qDebug() << ">>> PLC Init -> Enabled:" << plcConf.enabled
             << " IP:" << plcConf.ip
             << " Port:" << plcConf.port;

    // ============================================================
    // 【修复 5 - 核心】 必须调用 init 才能建立连接和启动轮询！
    // ============================================================
    m_plc->init(plcConf.enabled, plcConf.ip, plcConf.port);

    // 更新初始灯光状态
    if(plcConf.enabled) {
        updatePlcStatusIndicator(1); // 红色：启用但连接中
    } else {
        updatePlcStatusIndicator(0); // 灰色：已禁用
    }

    // 6. 初始化默认界面 (生成 4 个通道)
    onChannelCountChanged(4);

    // 增加一个标记位，防止每次收到数据都重复读文件
    m_hasLoadedSnFile = false;

    // [新增] 初始化扫码轮询定时器
    m_barcodePollTimer = new QTimer(this);
    m_barcodePollTimer->setInterval(200); // 每 200ms 检查一次文件
    connect(m_barcodePollTimer, &QTimer::timeout, this, &MainWindow::onBarcodePollTimeout);

}

MainWindow::~MainWindow()
{
    qDeleteAll(m_channels);
    m_channels.clear();
}

// ====================================================================
// [核心逻辑] 动态重建界面
// ====================================================================
void MainWindow::onChannelCountChanged(int count)
{
    // --- A. 安全检查 ---
    for(auto channel : qAsConst(m_channels)) {
        if(channel && channel->isTesting()) {
            QMessageBox::warning(this, "操作禁止",
                                 "检测正在进行中！\n请先停止所有通道的测试，再调整通道数量。");
            m_spinBoxCount->blockSignals(true);
            m_spinBoxCount->setValue(m_channels.size());
            m_spinBoxCount->blockSignals(false);
            return;
        }
    }

    // --- B. 清理旧现场 ---
    setUpdatesEnabled(false);
    qDeleteAll(m_channels);
    m_channels.clear();

    // --- C. 创建新通道并计算布局 ---
    int cols = (int)std::ceil(std::sqrt(count));
    if (count == 2) cols = 2;

    for(int i = 0; i < count; i++) {
        DeviceChannelWidget *w = new DeviceChannelWidget(i + 1, this);

        // [重要] 将 SN 管理器传给通道
       // w->setSnManager(m_snManager);

        // [新增] 连接身份上报信号 -> 主窗口的认领逻辑
        connect(w, &DeviceChannelWidget::identityReported,
                this, &MainWindow::onChannelIdentityReported);

        // [重要] 监听结果回写信号
        connect(w, &DeviceChannelWidget::testFinished, this, &MainWindow::onChannelTestFinished);

        m_channels.append(w);

        int row = i / cols;
        int col = i % cols;
        m_gridLayout->addWidget(w, row, col);
    }

    setUpdatesEnabled(true);
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    // 这里的逻辑其实已经被 eventFilter 接管了
    // 但保留着防止某些极端情况也没问题
    if(event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter) {
        m_scanBuffer.append(event->text());
        return;
    }

    QString code = m_scanBuffer.trimmed();
    m_scanBuffer.clear();
    if(code.isEmpty()) return;

    // 1. 先尝试直接匹配
    for(auto channel : qAsConst(m_channels)) {
        if(channel->checkScanInput(code) == ScanResult::Match) return;
    }

    // 2. 如果没匹配上 -> 加入缓存池
    if (!m_scanCache.contains(code)) {
        m_scanCache.append(code);
        qDebug() << ">>> [Scan Cache] Code added to pool:" << code;

        // 更新界面提示 (如果有 Label 的话)
        if(m_lblCacheStatus) m_lblCacheStatus->setText(QString("待匹配: %1").arg(m_scanCache.join(",")));
    }

    // 3. 错误提示
    QMessageBox::warning(this, "扫码错误",
                         QString("扫描内容: [%1]\n未找到匹配的设备！").arg(code));
}

void MainWindow::onChannelIdentityReported(const QString &idValue)
{
    // 这里的 idValue 就是设备刚发上来的 IMEI
    // sender() 是发出信号的那个通道对象
    DeviceChannelWidget* channel = qobject_cast<DeviceChannelWidget*>(sender());
    if (!channel) return;

    // 1. 去缓存池里找：有没有哪个码 等于 这个 IMEI？
    // (或者 contains，取决于您的码是不是完整的)
    int index = -1;
    for (int i = 0; i < m_scanCache.size(); ++i) {
        if (m_scanCache[i] == idValue) { // 这里也可以用 m_scanCache[i].contains(idValue) 做模糊匹配
            index = i;
            break;
        }
    }

    // 2. 如果找到了
    if (index != -1) {
        QString matchedCode = m_scanCache.takeAt(index); // 从池子里取出来，删掉

        qDebug() << ">>> [Match] Cache Hit! Channel" << channel->id() << "claimed code:" << matchedCode;

        // 3. 填入通道
        channel->setBarcode(matchedCode);

        // 4. 更新界面提示
        if(m_lblCacheStatus) m_lblCacheStatus->setText(QString("待匹配: %1").arg(m_scanCache.join(",")));
    }
}


// PLC 开始信号 -> 重置所有通道
// 在 MainWindow.cpp 中，替换原有的扫码逻辑
void MainWindow::onPlcStartSignal()
{
    qDebug() << ">>> [PLC] Start Signal (M1600) Received!";

    // 1. 【握手复位】 立即将 M1600 写回 0 (防止信号一直置位)
    if (m_plc) {
        m_plc->writeDevice(1600, false);
    }

    appendToLog(">>> [PLC] 收到启动信号，正在等待条码文件(SN.txt)...");

    // 2. 【UI 重置】
    for(auto channel : m_channels) {
        if (channel) channel->resetUI(false); // false = 不保留旧条码
    }

    if(m_lblCacheStatus) {
        m_lblCacheStatus->setText("状态: 等待扫码文件...");
        m_lblCacheStatus->setStyleSheet("color: blue; font-weight: bold;");
    }

    // 3. 【核心修改】 不再立即读，而是启动定时器去蹲守文件
    m_pollRetryCount = 0;
    m_barcodePollTimer->start();
}

void MainWindow::tryLoadBarcode()
{
    // 1. 确认路径 (建议改为从配置文件读取，这里先优化硬编码)
    QString filePath = "D:/SN.txt";
    QFile file(filePath);

    // 2. [诊断 A] 文件根本不存在
    if (!file.exists()) {
        // 只有当 PLC 已经启动且等待时间过长时才打日志，避免刷屏
        // appendToLog(">>> Debug: 等待 SN.txt 生成...");
        return;
    }

    // 3. [诊断 B] 文件被占用 (增加重试逻辑)
    bool isOpened = false;
    for (int i = 0; i < 3; ++i) { // 尝试 3 次
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            isOpened = true;
            break;
        }
        // 稍微等一下再试 (避开扫码软件写入锁定的那几毫秒)
        QThread::msleep(50);
    }

    if (!isOpened) {
        appendToLog(QString(">>> Error: 无法打开文件 %1 (可能被占用或权限不足)").arg(filePath));
        return;
    }

    // 4. [诊断 C] 编码问题 (强制兼容 GBK，防止乱码或空读)
    QTextStream in(&file);
    // 如果是 Windows 旧设备，通常是 GBK (Local8Bit)
    // 如果不确定，可以用 setCodec("GBK");
    in.setCodec("GBK");

    QString content = in.readAll().trimmed();
    file.close();

    // 5. [诊断 D] 文件是空的 (可能刚创建还没来得及写数据)
    if (content.isEmpty()) {
        appendToLog(">>> Warning: SN.txt 文件存在但内容为空");
        return;
    }

    // --- 读取成功，开始处理 ---
    appendToLog(QString(">>> [文件读取] 成功读取 SN.txt: %1").arg(content));

    // 解析逻辑 (保持你原有的 split 逻辑)
    content.replace('\r', " ").replace('\n', " ");
    QStringList snList = content.split(',', QString::KeepEmptyParts);

    // ... 后续分发给通道的逻辑 ...

    // 6. [防呆] 读取完后是否要删除？(MFC 逻辑通常会删，防止重复读)
    // file.remove();
}

// MainWindow.cpp

// MainWindow.cpp

void MainWindow::distributeBarcodeAndStart(const QString &rawContent)
{
    // 1. 数据清洗
    QString cleanContent = rawContent;
    cleanContent.replace("\r", "").replace("\n", "");

    // 2. 分割数据 (保持对应关系)
    QStringList parts = cleanContent.split(',', QString::KeepEmptyParts);

    // 清洗单个数据的 Lambda
    auto cleanPart = [](const QString &raw) {
        QString s = raw;
        s.replace("\"", "");
        s.replace("NOREAD", "");
        return s.simplified();
    };

    const auto idRules = ConfigManager::instance().getIdentityRules();

    // 3. 遍历所有通道
    for (int i = 0; i < m_channels.size(); ++i) {
        DeviceChannelWidget* channel = m_channels[i];
        if (!channel) continue;

        QString barcode = "UNKNOWN"; // 默认值
        QString cleanData = "";
        bool hasData = false;

        // 检查该位置是否有数据
        if (i < parts.size()) {
            cleanData = cleanPart(parts[i]);
            if (!cleanData.isEmpty()) {
                barcode = cleanData;
                hasData = true;
            }
        }

        // =========================================================
        // 分支 A: 有数据 (Channel 4)
        // =========================================================
        if (hasData) {
            channel->setBarcode(barcode);
            appendToLog(QString(">>> [分配] 通道 %1 <- %2").arg(channel->id()).arg(barcode));

            // 解析期望值 (IMEI/IMSI)
            for(const auto& rule : idRules) {
                if(!rule.enable) continue;
                QString pattern = QString("%1[:\\s]*([a-zA-Z0-9]+)")
                                      .arg(QRegularExpression::escape(rule.prefix));
                QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch match = re.match(cleanData);
                if (match.hasMatch()) {
                    channel->setExpectedIdentity(rule.key, match.captured(1));
                }
            }
        }
        // =========================================================
        // 分支 B: 无数据 (Channel 1, 2, 3)
        // =========================================================
        else {
            // 设置一个显眼的占位符，方便日志查询
            channel->setBarcode("UNKNOWN");
            // 清空旧的期望值，防止沿用上一次测试的 IMEI
            // (注意：您需要在 DeviceChannelWidget 里确保 startTest 或 resetUI 会清空 m_expectedIds)
            appendToLog(QString(">>> [分配] 通道 %1 无条码，强制启动 (SN=UNKNOWN)").arg(channel->id()));
        }

        // =========================================================
        // 【关键修改】 无论有没有数据，都统一启动！
        // =========================================================
        channel->startTest();
    }

    if(m_lblCacheStatus) {
        m_lblCacheStatus->setText("状态: 全通道测试运行中");
    }
}

// 通道测试结束 -> 回写 PLC
void MainWindow::onChannelTestFinished(int channelId, bool isPass, int failureReason)
{
    // 找到对应的通道对象
    DeviceChannelWidget* targetChannel = nullptr;
    for(auto ch : m_channels) {
        if(ch->id() == channelId) {
            targetChannel = ch;
            break;
        }
    }

    if (!targetChannel) return;

    // =================================================================
    // 【核心修复】 利用 Qt 动态属性缓存结果
    // =================================================================
    // 1. 标记该通道已完成
    targetChannel->setProperty("isFinished", true);
    // 2. 缓存该通道的最终结果 (true/false)
    targetChannel->setProperty("finalResult", isPass);

    qDebug() << ">>> [Progress] Channel" << channelId << "Finished. Pass:" << isPass;

    // =================================================================
    // 3. 检查是否 "所有" 通道都结束了
    // =================================================================
    bool allFinished = true;
    for (auto ch : m_channels) {
        // 如果有一个通道还没标记 isFinished，就继续等
        if (!ch->property("isFinished").toBool()) {
            allFinished = false;
            break;
        }
    }

    // 4. 如果全员到齐，触发上报
    if (allFinished) {
        finalizePlcResult();
    }
}

//结果上报PLC
void MainWindow::finalizePlcResult()
{
    if (!m_plc) return;

    appendToLog(">>> [结算] 所有通道处理完毕，开始分步上报 PLC...");

    bool hasGlobalImeiError = false;

    // --- 第一阶段：写入各通道结果 (M1655/M1660) ---
    for (int i = 0; i < m_channels.size(); ++i) {
        DeviceChannelWidget* ch = m_channels[i];
        if (!ch) continue;

        // 从属性中读取结果（包含空通道判定的结果）
        bool isPass = ch->property("finalResult").toBool();

        // 检查是否有 IMEI 混料错误
        if (ch->getFailureReason() == Reason_IMEI) {
            hasGlobalImeiError = true;
        }

        int addrOk = 1655 + i; // M1655+
        int addrNg = 1660 + i; // M1660+

        if (isPass) {
            m_plc->writeDevice(addrOk, true);  // 写 OK
            m_plc->writeDevice(addrNg, false); // 清 NG
        } else {
            m_plc->writeDevice(addrOk, false); // 清 OK
            m_plc->writeDevice(addrNg, true);  // 写 NG
        }
    }

    // 处理严重报警 M1665
    m_plc->writeDevice(1665, hasGlobalImeiError);
    if (hasGlobalImeiError) {
        appendToLog(">>> [PLC] 严重报警: 检测到 IMEI 混料 (M1665 ON)");
    }

    // --- 第二阶段：延时 200ms 后再写入放行信号 (M1650) ---
    // 增加物理延时，确保 PLC 扫描周期能捕捉到前面的状态切换
    QTimer::singleShot(200, this, [this](){
        if (m_plc) {
            appendToLog(">>> [PLC] 状态已稳定，发送流程结束信号 (M1650 ON)");
            m_plc->writeDevice(1650, true);
        }

        // 状态复位显示
        if(m_lblCacheStatus) {
            m_lblCacheStatus->setText("状态: 等待下一轮启动");
            m_lblCacheStatus->setStyleSheet("color: black;");
        }
    });
}

// 更新状态灯
void MainWindow::updatePlcStatusIndicator(int status)
{
    QString style, text;
    switch(status) {
    case 0: // Disabled
        style = "QLabel { background-color: #9E9E9E; color: white; border-radius: 4px; padding: 4px; font-weight: bold; }";
        text = "PLC: 已禁用";
        break;
    case 1: // Disconnected
        style = "QLabel { background-color: #F44336; color: white; border-radius: 4px; padding: 4px; font-weight: bold; }";
        text = "PLC: 未连接";
        break;
    case 2: // Connected
        style = "QLabel { background-color: #4CAF50; color: white; border-radius: 4px; padding: 4px; font-weight: bold; }";
        text = "PLC: 通信正常";
        break;
    }

    if(m_lblPlcStatus) {
        m_lblPlcStatus->setStyleSheet(style);
        m_lblPlcStatus->setText(text);
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // 只关心键盘按下事件
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

        // 1. 处理回车键 (扫码结束信号)
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {

            QString code = m_scanBuffer.trimmed();
            m_scanBuffer.clear(); // 清空缓存，准备下一次

            if (!code.isEmpty()) {
                qDebug() << ">>> [Scanner] Input Received:" << code;

                // --- 逻辑 A: 先尝试直接匹配 (万一串口数据已经有了) ---
                bool matched = false;
                for(auto channel : qAsConst(m_channels)) {
                    if(channel->checkScanInput(code) == ScanResult::Match) {
                        matched = true;
                        break;
                    }
                }

                // --- 逻辑 B: 没匹配上 -> 加入缓存池 (等待串口数据来认领) ---
                if (!matched) {
                    if (!m_scanCache.contains(code)) {
                        m_scanCache.append(code);
                        qDebug() << ">>> [Cache] Code added to pool:" << code;

                        // [可选] 如果您加了显示 Label，在这里更新
                        if(m_lblCacheStatus) m_lblCacheStatus->setText(QString("待匹配: %1").arg(m_scanCache.join(", ")));
                    } else {
                        qDebug() << ">>> [Cache] Duplicate code ignored.";
                    }
                }
            }

            // 重要：返回 true 表示“这个回车键我处理了，子控件别管了”
            // 防止回车键触发按钮点击或其他副作用
            return true;
        }
        // 2. 处理普通字符 (累加到缓存)
        else {
            // 只接收可见字符 (排除 Ctrl, Shift 等功能键)
            if (!keyEvent->text().isEmpty()) {
                m_scanBuffer.append(keyEvent->text());
            }
            // 这里返回 false，让事件继续传递，以免影响正常的打字输入(如果有的话)
            return false;
        }
    }

    // 其他事件交给父类默认处理
    return QMainWindow::eventFilter(obj, event);
}

// 辅助函数：从文本文件读取条码
void MainWindow::loadInitialBarcodeFromFile() {
    QString filePath = "D:/SN.txt";
    QFile file(filePath);

    // 如果打不开文件，打印警告并退出
    // 注意：如果 PLC 给了信号但文件没生成，这里会直接返回，导致不启动
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        appendToLog(">>> [文件] 警告: 无法打开 D:/SN.txt，测试未启动");
        return;
    }

    QTextStream in(&file);
    // 设置本地编码 (防止中文路径或乱码)
    in.setCodec(QTextCodec::codecForLocale());
    QString rawContent = in.readAll().trimmed(); // 加 trimmed 去掉首尾空白
    file.close();

    // 如果文件是空的，直接返回
    if (rawContent.isEmpty()) {
        appendToLog(">>> [文件] 警告: D:/SN.txt 内容为空，忽略本次启动");
        return;
    }

    appendToLog(QString(">>> [文件] 读取成功: %1").arg(rawContent));

    // 1. 按逗号分割通道数据
    // 假设 SN.txt 格式: "IMEI:123,MAC:456", "IMEI:789,MAC:000"
    QStringList parts = rawContent.split(',', QString::KeepEmptyParts);

    // 数据清洗 Lambda
    auto cleanPart = [](const QString &raw) {
        QString s = raw;
        s.replace("\"", "");      // 去引号
        s.replace("NOREAD", "");  // 去失败标记
        return s.simplified();    // 去首尾空格、换行
    };

    const auto idRules = ConfigManager::instance().getIdentityRules();
    int count = qMin(parts.size(), m_channels.size());

    // 2. 遍历通道进行填充并启动
    for (int i = 0; i < count; ++i) {
        DeviceChannelWidget* channel = m_channels[i];
        if (!channel) continue;

        QString cleanData = cleanPart(parts[i]);

        if (!cleanData.isEmpty()) {
            // A. 显示在左侧扫码框
            channel->setBarcode(cleanData);

            // B. 解析并设定期望值 (告诉通道等会儿要比对什么)
            for(const auto& rule : idRules) {
                if(!rule.enable) continue;

                // 正则提取：前缀 + 冒号/空格 + 值
                // 例如匹配 "IMEI:865357..."
                QString pattern = QString("%1[:\\s]*([a-zA-Z0-9]+)")
                                      .arg(QRegularExpression::escape(rule.prefix));
                QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch match = re.match(cleanData);

                if (match.hasMatch()) {
                    QString val = match.captured(1);
                    // 设定期望值
                    channel->setExpectedIdentity(rule.key, val);

                    qDebug() << ">>> [AutoLoad] Ch" << channel->id()
                             << " Expect: " << rule.key << "=" << val;
                }
            }

            // =========================================================
            // 【关键修复】 只有调用这个，通道才会打开串口并开始测试！
            // =========================================================
            appendToLog(QString(">>> [系统] 通道 %1 自动启动测试...").arg(channel->id()));
            channel->startTest();

        } else {
            // 如果该位置是空的 (例如 ",,")，通常意味着该通道不测试
            qDebug() << ">>> [AutoLoad] Ch" << channel->id() << " Data is Empty (Skip)";
            // 如果您希望即使没有条码也强行测试，可以在这里也调用 startTest()
        }
    }
}


void MainWindow::onAnyChannelIdentityReceived() {
    // 如果之前已经自动加载过文件了，就不要再读了
    if (m_hasLoadedSnFile) {
        return;
    }

    qDebug() << ">>> [Trigger] 收到串口身份数据，触发文件读取...";

    // 调用之前写好的读取函数
    loadInitialBarcodeFromFile();

    // 标记为已读取，防止反复触发
    m_hasLoadedSnFile = true;
}

// ===========================================================================
// [新增] 处理 PLC 停止信号 (M1650)
// ===========================================================================
void MainWindow::onPlcStopSignal()
{
    qDebug() << ">>> [PLC] Stop Signal (M1650) Received!";
    appendToLog(">>> [PLC] 收到强制停止信号，正在停止所有通道...");

    for(auto channel : m_channels) {
        if (channel) {
            // 调用重置，参数 false 表示不保留现有条码 (清空)
            channel->resetUI(false);
        }
    }

    if(m_lblCacheStatus) {
        m_lblCacheStatus->setText("状态: PLC 强制停止");
        m_lblCacheStatus->setStyleSheet("color: red; font-weight: bold;");
    }
}

// ===========================================================================
// [新增] 日志输出函数的具体实现
// ===========================================================================
void MainWindow::appendToLog(const QString &msg)
{
    // 1. 加上时间戳
    QString timeStr = QDateTime::currentDateTime().toString("[HH:mm:ss] ");
    QString fullMsg = timeStr + msg;

    // 2. 输出到 Qt 的调试控制台 (底部 "应用程序输出" 面板)
    qDebug() << fullMsg;

    // 3. (可选) 如果您的主界面上有一个用于显示总日志的文本框
    //    假设它的名字叫 m_logEdit 或者 ui->logEdit，请在这里取消注释并修改变量名

    // 例如：
    // if (m_logConsole) m_logConsole->appendPlainText(fullMsg);
}



 void MainWindow::onBarcodePollTimeout()
{
    QString filePath = "D:/SN.txt";
    QFile file(filePath);

    // 1. 文件检查 (保持原逻辑)
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_pollRetryCount++;
        checkBarcodeTimeout();
        return;
    }

    QTextStream in(&file);
    // 设置编码，防止中文乱码 (根据实际情况选择 "GBK" 或 "UTF-8")
    in.setCodec("GBK");
    QString content = in.readAll();
    file.close();

    if (content.trimmed().isEmpty()) {
        m_pollRetryCount++;
        checkBarcodeTimeout();
        return;
    }

    m_barcodePollTimer->stop();
    appendToLog(">>> [成功] 获取到文件内容，正在解析...");

    // 2. 扁平化处理
    QString flatContent = content;
    flatContent.replace('\r', " ").replace('\n', " ");
    QStringList snList = flatContent.split(',', QString::KeepEmptyParts);

    // =================================================================
    // 【核心逻辑修改】 遍历所有通道，空通道判 NG
    // =================================================================

    // 先初始化所有通道的状态为 "未完成"
    for(auto ch : m_channels) {
        if(ch) ch->setProperty("isFinished", false);
    }

    int totalChannels = m_channels.size();

    for (int i = 0; i < totalChannels; ++i) {
        DeviceChannelWidget* ch = m_channels[i];
        if (!ch) continue;

        ch->setProperty("isFinished", false); // 统一重置完成状态标记

        QString validSn = "";
        if (i < snList.size()) {
            // 【优化】提取时统一转大写，并去掉多余的换行与空格
            validSn = snList[i].trimmed().toUpper();
        }

        if (!validSn.isEmpty()) {
            // A. 有条码：正常启动测试
            appendToLog(QString(">>> [测试] 通道 %1 启动 (SN: %2)").arg(i + 1).arg(validSn));
            ch->startTestWithBarcode(validSn);
        }
        else {
            // B. 无条码：强制触发 NG 流程（保持 v10 的空条码日志记录逻辑）
            appendToLog(QString(">>> [跳过] 通道 %1 无条码，强制触发 NG 流程").arg(i + 1));
            ch->startTestWithBarcode("");
        }
    }
}

void MainWindow::checkBarcodeTimeout()
{
    // 200ms * 150次 = 30秒超时 (稍微给长一点)
    if (m_pollRetryCount > 150) {
        m_barcodePollTimer->stop();
        appendToLog(">>> [错误] 等待条码文件超时！请检查 D:/SN.txt 是否生成");

        // 上报 PLC 错误 (可选)
        // if (m_plc) m_plc->writeDevice(1665, true);
        // if (m_plc) m_plc->writeDevice(1650, true);
    }
}
