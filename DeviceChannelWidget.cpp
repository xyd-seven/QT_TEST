#include "DeviceChannelWidget.h"
#include "SnManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QDateTime>
#include <QSettings>

DeviceChannelWidget::DeviceChannelWidget(int id, QWidget *parent)
    : QWidget(parent), m_id(id)
{
    m_serial = new QSerialPort(this);

    // =============================================================
    // 【修复 1】 必须在这里 new 出对象，否则后面用的时候程序直接崩
    // =============================================================
    m_snManager = new SnManager(this);
    // 加载白名单 (确保 configs 目录下有 whitelist.csv，否则校验会失败)
    m_snManager->loadData("configs/sn_data.csv");

    setupUi();

    m_testTimer = new QTimer(this);
    m_testTimer->setSingleShot(true);
    connect(m_testTimer, &QTimer::timeout, this, &DeviceChannelWidget::onTestTimeout);

    // 连接串口信号
    connect(m_serial, &QSerialPort::readyRead, this, &DeviceChannelWidget::onSerialReadyRead);

    // =============================================================
    // 【优化】 移除 ConfigManager::instance().loadConfig(...)
    // ConfigManager 是单例，MainWindow 启动时已经加载过了，这里不要重复加载
    // =============================================================
}

// ====================================================================
// 1. 界面构建
// ====================================================================
void DeviceChannelWidget::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(2);

    m_group = new QGroupBox(QString("通道 %1").arg(m_id));
    setChannelStatus(true);

    QVBoxLayout *groupLayout = new QVBoxLayout(m_group);
    groupLayout->setContentsMargins(4, 6, 4, 4);
    groupLayout->setSpacing(4);

    // --- A. 顶部控制栏 ---
    QHBoxLayout *topLayout = new QHBoxLayout();

    m_cbModel = new QComboBox();
    QStringList configFiles = ConfigManager::getConfigFileList();
    if (configFiles.isEmpty()) {
        m_cbModel->addItem("默认配置");
        m_cbModel->setEnabled(false);
    } else {
        m_cbModel->addItems(configFiles);
        m_cbModel->setCurrentIndex(0);
        ConfigManager::instance().loadConfig(m_cbModel->currentText());
    }
    m_cbModel->setMaximumWidth(100);

    m_cbPort = new QComboBox();
    const auto ports = QSerialPortInfo::availablePorts();
    for(const auto &info : ports) m_cbPort->addItem(info.portName());
    m_cbPort->setMaximumWidth(80);

    // -----------------------------------------------------------------
    // [新增] 自动记忆功能开始
    // -----------------------------------------------------------------

    // 1. 定义配置文件的 Key (例如: Port_Ch1, Port_Ch2)
    QString configKey = QString("Port_Ch%1").arg(m_id);

    // 2. 读取上次保存的配置
    QSettings settings("AppConfig.ini", QSettings::IniFormat);
    QString savedPort = settings.value(configKey).toString();

    // 3. 尝试恢复选项
    if (!savedPort.isEmpty()) {
        int idx = m_cbPort->findText(savedPort);
        if (idx != -1) {
            // 如果上次存的端口现在还存在，就选中它
            m_cbPort->setCurrentIndex(idx);
        } else {
            // (可选) 如果上次的端口没插上，您可以选择什么都不做，或者把它加进去提示用户
            // m_cbPort->addItem(savedPort + "(离线)");
        }
    } else {
        // 如果是第一次运行，默认尝试选中 COM + 通道号 (例如通道1选COM1)
        int defaultIdx = m_cbPort->findText(QString("COM%1").arg(m_id));
        if (defaultIdx != -1) {
            m_cbPort->setCurrentIndex(defaultIdx);
        }
    }

    // 4. 连接信号：用户手动切换时，立即保存到文件
    // 注意：这里使用了 lambda 表达式捕获 configKey
    connect(m_cbPort, &QComboBox::currentTextChanged, this, [=](const QString &text){
        QSettings s("AppConfig.ini", QSettings::IniFormat);
        s.setValue(configKey, text); // 写入配置
    });

    // -----------------------------------------------------------------
    // [新增] 自动记忆功能结束
    // -----------------------------------------------------------------

    m_cbBaud = new QComboBox();
    m_cbBaud->addItems({"9600", "115200","460800", "921600"});
    m_cbBaud->setCurrentText("115200");
    m_cbBaud->setMaximumWidth(70);

    QPushButton *btnStart = new QPushButton("开启");
    QPushButton *btnStop = new QPushButton("停止");
    QPushButton *btnClear = new QPushButton("清空");
    btnStart->setStyleSheet("color: green; font-weight: bold;");
    btnStop->setStyleSheet("color: red;");
    btnStart->setMaximumWidth(50);
    btnStop->setMaximumWidth(50);
    btnClear->setMaximumWidth(50);

    topLayout->addWidget(new QLabel("机型:"));
    topLayout->addWidget(m_cbModel);
    topLayout->addWidget(new QLabel("端口:"));
    topLayout->addWidget(m_cbPort);
    topLayout->addWidget(new QLabel("波特:"));
    topLayout->addWidget(m_cbBaud);
    topLayout->addWidget(btnStart);
    topLayout->addWidget(btnStop);
    topLayout->addWidget(btnClear);
    topLayout->addStretch();

    // --- B. 身份比对区 (两行) ---
    QGridLayout *compareLayout = new QGridLayout();
    compareLayout->setContentsMargins(0, 0, 0, 0);
    compareLayout->setSpacing(4);

    QLabel *lblScan = new QLabel("扫码输入:");
    lblScan->setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
    m_editBarcode = new QLineEdit();
    m_editBarcode->setPlaceholderText("请扫描设备二维码...");
    m_editBarcode->setFont(QFont("Arial", 10));

    QLabel *lblRead = new QLabel("串口读取:");
    lblRead->setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
    m_editSerialRead = new QLineEdit();
    m_editSerialRead->setPlaceholderText("等待读取 IMEI/IMSI/MAC...");
    m_editSerialRead->setFont(QFont("Arial", 10));
    m_editSerialRead->setReadOnly(true);
    m_editSerialRead->setStyleSheet("background-color: #F0F0F0; color: #555;");

    compareLayout->addWidget(lblScan, 0, 0);
    compareLayout->addWidget(m_editBarcode, 0, 1);
    compareLayout->addWidget(lblRead, 1, 0);
    compareLayout->addWidget(m_editSerialRead, 1, 1);

    // --- C. 结果表格 (8列) ---
    m_tableRes = new QTableWidget();
    m_tableRes->setColumnCount(8);
    m_tableRes->setHorizontalHeaderLabels({"项", "值", "项", "值", "项", "值", "项", "值"});
    m_tableRes->verticalHeader()->setVisible(false);

    QHeaderView* header = m_tableRes->horizontalHeader();
    for(int k=0; k<4; k++) {
        header->setSectionResizeMode(k*2, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(k*2+1, QHeaderView::Stretch);
    }
    m_tableRes->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_tableRes->verticalHeader()->setDefaultSectionSize(26);

    // --- D. 日志窗口 (大容量) ---
    m_logView = new QPlainTextEdit();
    m_logView->setReadOnly(true);
    m_logView->setMinimumHeight(60);
    m_logView->setMaximumHeight(150);
    m_logView->setFont(QFont("Consolas", 9));
    m_logView->setMaximumBlockCount(3000);

    // --- E. 组装 ---
    groupLayout->addLayout(topLayout);
    groupLayout->addLayout(compareLayout);
    groupLayout->addWidget(m_tableRes);
    groupLayout->addWidget(m_logView);
    mainLayout->addWidget(m_group);

    // --- F. 信号 ---
    connect(btnStart, &QPushButton::clicked, this, &DeviceChannelWidget::onStartClicked);
    connect(btnStop, &QPushButton::clicked, this, &DeviceChannelWidget::onStopClicked);
    connect(m_serial, &QSerialPort::readyRead, this, &DeviceChannelWidget::onSerialReadyRead);
    connect(m_editBarcode, &QLineEdit::textChanged, this, &DeviceChannelWidget::onBarcodeChanged);

    connect(m_cbModel, &QComboBox::currentTextChanged, this, [=](const QString &fileName){
        if (fileName.isEmpty() || fileName == "默认配置") return;
        m_logView->appendPlainText(QString(">>> Load: %1").arg(fileName));
        ConfigManager::instance().loadConfig(fileName);
        resetUI();
    });

    connect(btnClear, &QPushButton::clicked, this, [=](){
        m_buffer.clear();
        m_logView->clear();
        resetUI();
    });
}

// ====================================================================
// 2. 界面重置
// ====================================================================
void DeviceChannelWidget::resetUI(bool keepBarcode) {
    // 0. 停止上一轮计时
    if (m_testTimer->isActive()) {
        m_testTimer->stop();
    }

    // 1. 清空数据容器
    m_expectedIds.clear();
    m_currentIds.clear();
    m_buffer.clear();
    m_mapResRow.clear(); // [修复] 记得清空行索引缓存，否则表格重绘会乱

    // 2. 重置状态
    m_hasError = false;
    m_isImeiMismatch = false;
    m_isTesting = false;
    m_lastResetTime = QDateTime::currentMSecsSinceEpoch();

    // =========================================================
    // 3. 界面元素重置
    // =========================================================

    // 3.1 清空串口显示区 (这是必须的，因为是新测试)
    // 先手动清空显示部分，防止 updateSerialDisplay 还有旧缓存
    if(m_editSerialRead) {
        m_editSerialRead->clear();
        m_editSerialRead->setStyleSheet("background-color: #F0F0F0; color: #555;");
    }

    // =============================================================
    // 【逻辑修复】 根据传入参数决定是否清空条码
    // PLC 自动启动时：调用 resetUI(false) -> 清空旧条码 (随后 MainWindow 会填新条码)
    // 人工点击清空时：调用 resetUI(false) -> 清空一切
    // 如果未来有“保留条码重测”的需求：调用 resetUI(true)
    // =============================================================
    if (!keepBarcode && m_editBarcode) {
        m_editBarcode->clear();
        m_editBarcode->setStyleSheet("");
    }

    // =========================================================
    // 4. 重建表格 (逻辑保持不变)
    // =========================================================
    auto teleRules = ConfigManager::instance().getTelemetryRules();
    // 向上取整计算行数
    int rows = (teleRules.size() + 3) / 4;

    m_tableRes->clear();
    m_tableRes->setRowCount(rows);
    m_tableRes->setColumnCount(8);
    m_tableRes->setHorizontalHeaderLabels({"项", "值", "项", "值", "项", "值", "项", "值"});
    m_tableRes->verticalHeader()->setVisible(false);

    m_mapResRow.clear();

    for(int i=0; i<teleRules.size(); i++) {
        int r = i / 4;
        int c_base = (i % 4) * 2;

        QTableWidgetItem *head = new QTableWidgetItem(teleRules[i].name);
        head->setBackground(QColor(240, 240, 240)); // 浅灰背景
        head->setFont(QFont("Microsoft YaHei", 9));
        head->setFlags(Qt::ItemIsEnabled); // 设为只读
        m_tableRes->setItem(r, c_base, head);

        QTableWidgetItem *resItem = new QTableWidgetItem("WAIT");
        resItem->setTextAlignment(Qt::AlignCenter);
        resItem->setFont(QFont("Arial", 9));
        resItem->setBackground(Qt::white);
        resItem->setForeground(Qt::black);
        m_tableRes->setItem(r, c_base + 1, resItem);

        m_mapResRow[teleRules[i].key] = i;
    }

    // 5. 日志清空
    if(m_logView) {
        m_logView->clear();
        m_logView->appendPlainText("--- 等待开始 ---");
    }

    // 恢复默认状态 (白色背景)，表示空闲/等待
    setChannelStatus(false);
}

// ====================================================================
// 3. 串口数据处理
// ====================================================================
void DeviceChannelWidget::onSerialReadyRead() {
    QByteArray data = m_serial->readAll();

    // ========================================================
    // [新增] 实时写入文件
    // ========================================================
    if (m_logFile && m_logFile->isOpen()) {
        m_logFile->write(data);
        m_logFile->flush(); // 立即刷新，防止程序崩溃数据丢失
    }
    // ========================================================

    if (!m_isTesting) {
        // 可选：把缓冲区读空丢弃，防止下次启动时读到旧数据
        m_serial->readAll();
        return;
    }

    m_buffer.append(data);

    // 防止缓存爆炸 (保留最近 20KB)
    if(m_buffer.size() > 20480) m_buffer.clear();

    processBuffer();
}

void DeviceChannelWidget::processBuffer() {
    while(true) {
        // 同时查找 \n 和 \r
        int idxN = m_buffer.indexOf('\n');
        int idxR = m_buffer.indexOf('\r');
        int idx = -1;

        // 找最早出现的换行符
        if (idxN != -1 && idxR != -1) idx = qMin(idxN, idxR);
        else if (idxN != -1) idx = idxN;
        else if (idxR != -1) idx = idxR;
        else break; // 没找到换行符，退出等待更多数据

        // 提取一行
        QString line = QString::fromLocal8Bit(m_buffer.left(idx)).trimmed();

        // 移除已处理的数据 (包括换行符自己)
        m_buffer.remove(0, idx + 1);

        if(!line.isEmpty()) {
            parseLine(line);
            // 只有非空行才记录日志，避免日志里全是空行
            m_logView->appendPlainText(line);
        }
    }
}

// ====================================================================
// 4. 核心解析逻辑 (修复重复重置BUG)
// ====================================================================
void DeviceChannelWidget::parseLine(const QString &line) {
    if(!m_isTesting) return;

    // 预处理
    QString cleanLine = line.trimmed();
    if (cleanLine.isEmpty()) return;

    // =======================================================
    // A. [保留原有功能] 遥测数据处理 ($info)
    // =======================================================
    if(cleanLine.contains("$info,")) {
        int start = cleanLine.indexOf("$info,");
        // 调用您原有的 parseTelemetry 函数
        parseTelemetry(cleanLine.mid(start + 6));
        return;
    }

    // B. 获取规则
    const auto idRules = ConfigManager::instance().getIdentityRules();
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool anyUpdate = false;

    // C. 遍历规则
    for(const auto& rule : idRules) {
        if(!rule.enable) continue;

        if(cleanLine.startsWith(rule.prefix, Qt::CaseInsensitive)) {
            QString key = rule.key.toUpper();

            // 提取值
            QString val = cleanLine.mid(rule.prefix.length()).trimmed();
            if (val.startsWith(":")) val = val.mid(1).trimmed();
            if(val.isEmpty()) continue;

            // [1. 去重机制]
            if (m_currentIds.value(key) == val) {
                if (now - m_lastResetTime < 8000) continue;
            }

            qDebug() << ">>> [Serial Recv] Channel" << m_id << "Got:" << key << "=" << val;

            // =======================================================
            // 【修改点 1】 立即更新数据和显示 (无论对错)
            // =======================================================
            // 这样即使比对失败，界面上也能看到刚刚读到的错误条码
            m_currentIds.insert(key, val);
            updateSerialDisplay();
            anyUpdate = true;

            // [注意] 已移除 emit identityDetected(); 改为 PLC 启动时主动加载

            // =======================================================
            // [2. 期望值严格比对]
            // =======================================================
            if (m_expectedIds.contains(key)) {
                QString expected = m_expectedIds.value(key);

                if (val != expected) {
                    // 【修改点 2】 发现错误：只标记，不退出
                    m_hasError = true;

                    // 标记严重错误类型
                    if (key == "IMEI") {
                        m_isImeiMismatch = true;
                    }

                    // 记录红字日志
                    m_logView->appendPlainText(QString(">>> ERROR: %1 不匹配!").arg(rule.name));
                    m_logView->appendPlainText(QString("    期望: [%1]").arg(expected));
                    m_logView->appendPlainText(QString("    实际: [%1]").arg(val));

                    // 界面变红
                    setChannelStatus(false);

                    // 【关键】 绝对不要在这里 emit testFinished 或 return
                    // 让程序继续跑，直到超时定时器触发
                } else {
                    m_logView->appendPlainText(QString(">>> OK: %1 匹配成功").arg(rule.name));
                }
            }

            // [3. 启动超时计时器] (如果没启动)
            if (!m_testTimer->isActive()) {
                int timeoutMs = ConfigManager::instance().getTestTimeout();
                m_testTimer->start(timeoutMs);
                m_logView->appendPlainText(QString(">>> 测试开始，倒计时: %1 秒").arg(timeoutMs/1000.0));
            }

            // =======================================================
            // [保留原有功能] 业务逻辑 (IMEI上报 & SN校验)
            // =======================================================
            if (key == "IMEI") emit identityReported(val);

            // --- SN 校验逻辑 (保留您之前的 SnManager 代码) ---
            if (key == "IMSI") {
                bool isSnCheckEnabled = ConfigManager::instance().isSnVerificationEnabled();

                if (m_snManager && isSnCheckEnabled) {
                    QString outSn;
                    bool isLegit = m_snManager->checkIdentity(val, outSn);

                    // =================================================================
                    // 【逻辑分支 A】: 合法设备 (白名单校验通过)
                    // =================================================================
                    if (isLegit) {
                        // 1. 【核心修改】 收到正确数据，尝试“挽救”/“复位”错误状态
                        // 只有当之前的错误是“混料/非法设备”导致时，才尝试清除
                        // 这样就实现了“发现错误变红 -> 再次收到正确数据 -> 变绿”的效果
                        if (m_isImeiMismatch) {
                            m_hasError = false;       // 清除通用错误标记
                            m_isImeiMismatch = false; // 清除混料标记
                            if (m_logView) m_logView->appendPlainText(">>> Info: 收到正确数据，错误状态已清除");
                        }

                        // 2. 关联 SN
                        if (!outSn.isEmpty()) {
                            m_currentIds.insert("SN", outSn);

                            // 3. 进一步检查：白名单查出的 SN 是否与文件里期望的 SN 一致？
                            if (m_expectedIds.contains("SN")) {
                                if (m_expectedIds.value("SN") != outSn) {
                                    // 虽然白名单里有，但不是我们要测的那个（比如扫码扫的是A，串口发的是B）
                                    m_hasError = true;
                                    if (m_logView) m_logView->appendPlainText(">>> Error: SN 不匹配 (白名单 vs 文件)");
                                }
                            }
                        }

                        // 4. 刷新界面 (如果 m_hasError 被清除了，这里就会变绿)
                        updateSerialDisplay();
                    }
                    // =================================================================
                    // 【逻辑分支 B】: 非法设备 (不在白名单)
                    // =================================================================
                    else {
                        // 1. 标记错误
                        m_hasError = true;
                        m_isImeiMismatch = true;

                        // 2. 打印日志
                        if (m_logView) {
                            m_logView->appendPlainText(QString(">>> Error: 非法 IMSI: %1 (不在白名单)").arg(val));
                        }

                        // 3. 立即刷新界面 (变红)
                        updateSerialDisplay();

                        // 4. 【保持之前的修改】 不中断测试，允许重复接收
                    }
                }
            }

            // 处理完这一行就跳出循环
            break;
        }
    }

    // D. 尝试判定结果 (仅当无错误时才尝试提前 Pass)
    // 如果 m_hasError 为 true，这里即使数据全了也不会进，会一直等到超时
    if (anyUpdate && !m_hasError) {
        performComparison();
    }
}


// ====================================================================
// 5. 动态显示与比对 (已修复大小写问题)
// ====================================================================
void DeviceChannelWidget::updateSerialDisplay()
{
    QStringList displayParts;
    const auto idRules = ConfigManager::instance().getIdentityRules();

    // 1. 构造串口数据显示内容
    for(const auto& rule : idRules) {
        QString searchKey = rule.key.toUpper();
        if(m_currentIds.contains(searchKey)) {
            displayParts << QString("%1:%2").arg(searchKey, m_currentIds[searchKey]);
        }
    }
    QString fullInfo = displayParts.join(" ");
    m_editSerialRead->setText(fullInfo);

    // =========================================================
    // 【核心修复】 UI 颜色判定逻辑 (v10.1)
    // =========================================================

    // 默认状态：灰色 (等待数据)
    QString style = "background-color: #F0F0F0; color: #555; border: 1px solid #CCC;";

    // A. 如果已经出现了明确的错误 (如超时、混料) -> 红色
    if (m_hasError) {
        style = "background-color: #F2DEDE; color: #A94442; font-weight: bold; border: 2px solid red;";
    }
    // B. 如果有期望值 (即通过 CSV 映射了关系) -> 比较 期望值 vs 实际值
    else if (!m_expectedIds.isEmpty()) {
        bool allMatched = true;
        // 遍历所有期望的 ID (例如 IMEI)
        for(auto it = m_expectedIds.begin(); it != m_expectedIds.end(); ++it) {
            QString key = it.key();
            QString expVal = it.value(); // 这是从 CSV 查出来的 865...

            // 如果串口还没读到这个 Key，或者读到的值不等于期望值
            if (m_currentIds.value(key) != expVal) {
                allMatched = false;
                break;
            }
        }

        if (allMatched) {
            // 完全匹配 -> 绿色
            style = "background-color: #DFF0D8; color: #3C763D; font-weight: bold; border: 2px solid green;";
        }
    }
    // C. 如果没有期望值 (盲测模式) -> 只要有数据就暂定为绿色，除非有错误
    else if (!m_currentIds.isEmpty()) {
        style = "background-color: #DFF0D8; color: #3C763D; font-weight: bold; border: 2px solid green;";
    }

    m_editSerialRead->setStyleSheet(style);
}

void DeviceChannelWidget::onBarcodeChanged(const QString &text) {
    Q_UNUSED(text);
    updateSerialDisplay();
}

// ====================================================================
// 6. 辅助函数
// ====================================================================
void DeviceChannelWidget::onStartClicked() {
    // 1. 先关闭旧的（如果有）
    if(m_serial->isOpen()) m_serial->close();

    // 关闭旧日志文件
    if(m_logFile) {
        if(m_logFile->isOpen()) m_logFile->close();
        delete m_logFile;
        m_logFile = nullptr;
    }

    m_serial->setPortName(m_cbPort->currentText());
    m_serial->setBaudRate(m_cbBaud->currentText().toInt());

    if(m_serial->open(QIODevice::ReadWrite)) {
        m_logView->appendPlainText("--- 端口已打开 ---");

        // ========================================================
        // [新增] 创建日志文件逻辑
        // 路径格式: Logs/20260114/Ch1_123045.txt
        // ========================================================
        QString dirPath = QString("Logs/%1").arg(QDate::currentDate().toString("yyyyMMdd"));
        QDir dir;
        if(!dir.exists(dirPath)) dir.mkpath(dirPath); // 自动创建目录

        QString fileName = QString("%1/Ch%2_%3.txt")
                               .arg(dirPath)
                               .arg(m_id)
                               .arg(QDateTime::currentDateTime().toString("HHmmss"));

        m_logFile = new QFile(fileName);
        if(m_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            m_logView->appendPlainText(QString(">>> Log: %1").arg(fileName));
        } else {
            m_logView->appendPlainText(">>> Warning: 创建日志文件失败!");
        }
        // ========================================================
        resetUI();
        m_isTesting = true;
    } else {
        m_logView->appendPlainText("错误: 打开串口失败!");
    }
}

// 停止按钮
void DeviceChannelWidget::onStopClicked() {
    if(m_serial->isOpen()) {
        m_serial->close();
        m_isTesting = false;
        m_logView->appendPlainText("--- 端口已关闭 ---");

        // [新增] 关闭文件
        if(m_logFile) {
            if(m_logFile->isOpen()) m_logFile->close();
            delete m_logFile;
            m_logFile = nullptr;
        }

        setChannelStatus(true);
    }
}

// 析构函数
DeviceChannelWidget::~DeviceChannelWidget() {
    if (m_serial->isOpen()) m_serial->close();

    // [新增] 安全关闭文件
    if (m_logFile) {
        if (m_logFile->isOpen()) m_logFile->close();
        delete m_logFile;
    }
}

void DeviceChannelWidget::updateResultItem(const QString &key, const QString &val) {
    if(!m_mapResRow.contains(key)) return;
    int index = m_mapResRow[key];
    int row = index / 4;
    int col = (index % 4) * 2 + 1;

    QTableWidgetItem *item = m_tableRes->item(row, col);
    if(!item) return;

    auto rules = ConfigManager::instance().getTelemetryRules();
    if(index >= rules.size()) return;
    TestRule rule = rules[index];

    if (rule.type == Type_Display) {
        item->setText(val);
        item->setForeground(QBrush(QColor(0, 0, 200)));
        return;
    }
    if (item->text() == "OK") return;

    bool pass = false;
    double numVal = val.toDouble();
    if(rule.type == Type_Match) pass = (val == rule.targetVal);
    else if (rule.type == Type_NotMatch) pass = (val != rule.targetVal);
    else if (rule.type == Type_Range) pass = (numVal >= rule.minVal && numVal <= rule.maxVal);
    else pass = (val != "0" && !val.isEmpty());

    if(pass) {
        item->setText("OK");
        item->setBackground(QBrush(Qt::white));
        item->setForeground(QBrush(QColor(0, 150, 0)));
        item->setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
    } else {
        item->setText(QString("NG (%1)").arg(val));
        item->setBackground(QBrush(QColor(255, 0, 0)));
        item->setForeground(QBrush(Qt::white));
        item->setFont(QFont("Arial", 8));
    }
}

void DeviceChannelWidget::performComparison() {
    // [DEBUG] 1. 打印时间戳，方便追踪
    // qDebug() << QString("\n=== [Channel %1] 状态判定 (%2) ===")
    //                 .arg(m_id)
    //                 .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"));

    // =================================================================
    // 步骤 A: 检查 "身份期望值" (源自 D:/SN.txt)
    // =================================================================
    bool identityPass = true;

    // 如果有期望值（说明文件加载成功），必须逐个比对
    if (!m_expectedIds.isEmpty()) {
        for(auto key : m_expectedIds.keys()) {
            QString expected = m_expectedIds.value(key);
            QString current = m_currentIds.value(key); // 当前读到的

            // 1. 没读到
            if (current.isEmpty()) {
                // qDebug() << "   -> [身份] 等待获取:" << key;
                identityPass = false;
                break;
            }

            // 2. 读到了但不匹配
            if (current != expected) {
                qDebug() << "   -> [身份] ❌ 不匹配:" << key << "期望:" << expected << "实际:" << current;
                // 注意：这里不强制设 m_hasError，因为 parseLine 里已经处理过报错了
                // 这里主要用于阻断 Pass
                identityPass = false;
                break;
            }
        }
    }

    // =================================================================
    // 步骤 B: 检查 "遥测规则" (源自 Config / 界面表格)
    // =================================================================
    auto rules = ConfigManager::instance().getTelemetryRules();
    bool telemetryAllRecv = true; // 遥测数据是否齐了
    bool telemetryHasNG = false;  // 是否有 NG 项

    for(int i = 0; i < rules.size(); ++i) {
        const auto& rule = rules[i];
        if(!rule.enable) continue;

        // 获取表格里的状态
        int row = i / 4;
        int col = (i % 4) * 2 + 1;
        QTableWidgetItem *item = m_tableRes->item(row, col);
        QString currentText = item ? item->text() : "NULL";

        // Display 类型不参与判定
        if(rule.type == Type_Display) continue;

        // 1. 检查是否等待中
        if(!item || item->text() == "WAIT" || item->text().isEmpty()) {
            // qDebug() << "   -> [遥测] 等待数据:" << rule.name;
            telemetryAllRecv = false;
            continue;
        }

        // 2. 检查是否 NG
        if(item->text().startsWith("NG")) {
            qDebug() << "   -> [遥测] 发现 NG:" << rule.name << "值:" << item->text();
            telemetryHasNG = true;
        }
    }

    // =================================================================
    // 步骤 C: 最终综合判定
    // =================================================================
    // 判定通过的条件：
    // 1. 没有任何错误标记 (m_hasError 为 false)
    // 2. 身份比对全部一致 (identityPass 为 true)
    // 3. 遥测数据全部收齐 (telemetryAllRecv 为 true)
    // 4. 遥测没有 NG 项 (telemetryHasNG 为 false)

    if (!m_hasError && identityPass && telemetryAllRecv && !telemetryHasNG) {

        qDebug() << ">>> [结果] ✅ Channel" << m_id << "所有条件满足 -> 触发 PASS";

        // 1. 停止倒计时
        if (m_testTimer->isActive()) m_testTimer->stop();

        // 2. 界面变绿
        setChannelStatus(true);

        // 3. 发送信号 (使用 3 参数版本)
        // 参数: ID, 是否通过, 失败原因(无)
        emit testFinished(m_id, true, Reason_None);

        m_logView->appendPlainText(">>> 最终结果: PASS (提前完成)");
        m_isTesting = false; // 锁定，防止后续数据干扰
    }
    // 否则：不做任何操作，继续等待下一次串口数据或超时
}

void DeviceChannelWidget::setChannelStatus(bool active) {
    if(active) m_group->setStyleSheet("QGroupBox { border: 2px solid green; font-weight: bold; margin-top: 1ex; } QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; }");
    else m_group->setStyleSheet("QGroupBox { border: 2px solid red; font-weight: bold; margin-top: 1ex; } QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; }");
}

void DeviceChannelWidget::parseTelemetry(const QString &dataPart) {
    // 原始数据示例: "... v:4.211,t:0.776,35,pwr:1 ..."

    // 1. 先按逗号粗暴分割
    // 结果变成 list: ["v:4.211", "t:0.776", "35", "pwr:1"]
    const QStringList parts = dataPart.split(',', Qt::SkipEmptyParts);

    bool isNextT2 = false; // 标记位：下一个纯数字是否为 t2
    bool anyUpdate = false;

    for(const QString &part : parts) {
        QString cleanPart = part.trimmed();

        // --- 情况 A: 标准的 key:value (例如 t:0.776) ---
        if(cleanPart.contains(':')) {
            QStringList kv = cleanPart.split(':');
            if(kv.size() == 2) {
                QString key = kv[0].trimmed();
                QString val = kv[1].trimmed();

                // [特殊处理] 如果检测到 key 是 "t"
                if(key.compare("t", Qt::CaseInsensitive) == 0) {
                    // 1. 把前半部分(0.776) 映射给 "t1"
                    updateResultItem("t1", val);

                    // 2. 标记：下一个没有冒号的数字，就是 "t2"
                    isNextT2 = true;
                }
                else {
                    // 其他普通项 (v, pwr, mac...)
                    updateResultItem(key, val);
                    isNextT2 = false; // 重置标记
                }
                anyUpdate = true;
            }
        }
        // --- 情况 B: 没有冒号的纯数值 (例如 35) ---
        else if(!cleanPart.isEmpty()) {
            // 如果上一个项是 t，那这个项肯定是 t2
            if(isNextT2) {
                updateResultItem("t2", cleanPart);
                isNextT2 = false; // 用完即焚，防止误判
                anyUpdate = true;
            }
        }
    }

    if(anyUpdate) performComparison();
}

ScanResult DeviceChannelWidget::checkScanInput(const QString &code) {
    QString mySerialData = m_editSerialRead->text().trimmed();

    // 情况 A: 还没有串口数据，或者没开机
    if(mySerialData.isEmpty()) {
        // 如果光标在这个框里，用户非要扫，那就先把码填进去，显示灰色等待
        if(m_editBarcode->hasFocus()) {
            m_editBarcode->setText(code);
            return ScanResult::Mismatch; // 暂时算错，或者你可以定义一个 Wait 状态
        }
        return ScanResult::Ignore;
    }

    // 情况 B: 完美匹配 (Auto-Routing)
    // 逻辑：不管光标在不在我这，只要码对上了，我就认领
    if(code == mySerialData) { // 或者 contains
        m_editBarcode->setText(code);
        updateSerialDisplay(); // 触发变绿
        return ScanResult::Match;
    }

    // 情况 C: 不匹配，但是光标在我这里 (Focus Trap)
    // 逻辑：光标在我这，说明用户就是在测我，但是扫错了 -> 判 NG
    if(m_editBarcode->hasFocus()) {
        m_editBarcode->setText(code);
        updateSerialDisplay(); // 触发变红
        return ScanResult::Mismatch;
    }

    // 情况 D: 既不匹配，光标也不在我这 -> 跟我无关
    return ScanResult::Ignore;
}

// DeviceChannelWidget.cpp

void DeviceChannelWidget::onTestTimeout() {
    // 0. [安全检查] 如果已经不在测试状态，直接退出，防止多次触发
    if (!m_isTesting) return;

    // 1. [核心] 立即锁定状态位
    // 必须在做任何其他事情之前设为 false，这能配合 onSerialReadyRead 拦截后续数据
    m_isTesting = false;
    m_hasError = true;

    // 2. [核心] 物理关闭串口
    // 这是“停止打印”最有效的手段。不仅停止了接收，也释放了硬件资源。
    if (m_serial->isOpen()) {
        m_serial->close();
    }

    // 3. 记录日志 (界面 + 文件)
    QString err = ">>> [Timeout] 测试超时！强制停止串口接收。";

    if (m_logView) {
        m_logView->appendPlainText(err);
    }

    if (m_logFile && m_logFile->isOpen()) {
        m_logFile->write(err.toUtf8() + "\n");
        m_logFile->flush();
    }

    // 4. 确保界面变红 (Visual Feedback)
    // 假设 setChannelStatus(false) 会把 GroupBox 变红
    setChannelStatus(false);
    // 如果 setChannelStatus 没变色，您可以手动补一句：
    // m_group->setStyleSheet("QGroupBox { background-color: #ffcccc; }");

    // 5. 确定失败原因
    int reason = Reason_Common; // 默认为普通错误(超时/漏测)

    if (m_isImeiMismatch) {
        reason = Reason_IMEI;   // 如果之前是因为 IMEI 错导致的卡死，上报严重错误
    }

    // 6. 发送信号给 MainWindow 进行最终汇总
    emit testFinished(m_id, false, reason);
}

void DeviceChannelWidget::setBarcode(const QString &text)
{
    // 假设您的 UI 里有一个 m_lineEditScan 或者 m_cbBarcode 用于显示条码
    if (m_editBarcode) {
        // 1. 临时屏蔽信号 (防止 setText 触发 textChanged 信号导致死循环或误操作)
        bool oldState = m_editBarcode->blockSignals(true);

        // 2. 设置文本
        m_editBarcode->setText(text);

        // 3. 恢复信号
        m_editBarcode->blockSignals(oldState);
    }

}

void DeviceChannelWidget::setExpectedIdentity(const QString &key, const QString &value)
{
    // 转为大写 key 统一存储，防止大小写差异
    // trimmed() 防止隐形空格
    m_expectedIds.insert(key.toUpper(), value.trimmed());
}

QString DeviceChannelWidget::getBarcode() const {
    if (m_editBarcode) {
        return m_editBarcode->text();
    }
    return QString();
}
// ===========================================================================
// [新增/补全] 启动测试函数
// ===========================================================================
// DeviceChannelWidget.cpp

void DeviceChannelWidget::startTest(bool keepBarcode)
{
    // =========================================================
    // 1. 【防重入检查】 (保持你原有的逻辑)
    // =========================================================
    if (m_isTesting) {
        if (m_logView) m_logView->appendPlainText(">>> [警告] 测试正在进行中，忽略重复启动请求。");
        return;
    }

    // =========================================================
    // 2. 【核心修复：界面重置与条码保护】
    // =========================================================
    // 这里是将 resetUI 收敛进来的最佳位置。
    // 必须在打开串口之前、检查完忙碌状态之后执行。

    QString savedSn;

    // A. 如果需要保留条码，先备份
    if (keepBarcode && m_editBarcode) {
        savedSn = m_editBarcode->text();
    }

    // B. 执行标准重置 (清空表格、变灰、清空LogView、清空输入框)
    // 请确保你有这个函数，如果没有，把 m_editBarcode->clear() 等逻辑写在这里
    resetUI();

    // C. 如果备份了条码，现在填回去
    if (keepBarcode && m_editBarcode) {
        m_editBarcode->setValidator(nullptr); // 再次确保移除限制
        m_editBarcode->setText(savedSn);      // 填回条码
    }


    // =========================================================
    // 3. 【硬件启动逻辑】 (你原有的代码)
    // =========================================================

    // 确保串口是打开的
    if (!m_serial->isOpen()) {
        if (m_cbPort && m_cbBaud) {
            m_serial->setPortName(m_cbPort->currentText());
            m_serial->setBaudRate(m_cbBaud->currentText().toInt());

            if (!m_serial->open(QIODevice::ReadWrite)) {
                // 注意：如果打开失败，这里的 log 可能看不见(因为刚才 resetUI 清空了)
                // 所以建议 resetUI 时不要清空 logView，或者在这里重新 append
                if (m_logView) m_logView->appendPlainText(">>> Error: 无法打开串口，测试无法启动!");
                return;
            }
        }
    }

    // 设置状态位 (落锁)
    m_isTesting = true;

    // 创建日志文件
    createLogFile();

    if (m_logView) {
        m_logView->appendPlainText(">>> 测试已启动 (监听串口数据...)");
    }

    // 启动超时倒计时
    if (m_testTimer) {
        int timeoutMs = ConfigManager::instance().getTestTimeout();
        m_testTimer->start(timeoutMs);
        if (m_logView) {
            m_logView->appendPlainText(QString(">>> 超时倒计时已启动: %1 秒").arg(timeoutMs / 1000.0));
        }
    }
}

// ===========================================================================
    // [防漏补缺] 如果之前没有添加 createLogFile，请把这个也加上
    // ===========================================================================
void DeviceChannelWidget::createLogFile() {
    // 1. 关闭旧日志
    if(m_logFile) {
        if(m_logFile->isOpen()) m_logFile->close();
        delete m_logFile;
        m_logFile = nullptr;
    }

    // 2. 生成路径 Logs/20260125/Ch1_123045.txt
    QString dirPath = QString("Logs/%1").arg(QDate::currentDate().toString("yyyyMMdd"));
    QDir dir;
    if(!dir.exists(dirPath)) dir.mkpath(dirPath);

    QString fileName = QString("%1/Ch%2_%3.txt")
                           .arg(dirPath)
                           .arg(m_id)
                           .arg(QDateTime::currentDateTime().toString("HHmmss"));

    // 3. 打开新文件
    m_logFile = new QFile(fileName);
    if(m_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (m_logView) m_logView->appendPlainText(QString(">>> Log: %1").arg(fileName));
    } else {
        if (m_logView) m_logView->appendPlainText(">>> Warning: 创建日志文件失败!");
    }
}

    // 在 DeviceChannelWidget.cpp 末尾添加

void DeviceChannelWidget::startTestWithBarcode(const QString &sn)
{
    // 1. 界面与内部状态重置
    if (m_editBarcode) {
        m_editBarcode->setValidator(nullptr);
        m_editBarcode->setText(sn.isEmpty() ? "NO_BARCODE" : sn);
    }

    // 2. 启动测试（内部会调用 resetUI 和 createLogFile）
    // 传入 true 表示保留刚才设置的条码显示
    startTest(true);

    // 3. 核心逻辑：如果是空条码，立即在日志中记录并判定为 NG
    if (sn.isEmpty()) {
        if (m_logFile && m_logFile->isOpen()) {
            QTextStream out(m_logFile);
            out << "[" << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "] "
                << "Error: No barcode received from SN.txt. Terminating as NG." << "\n";
        }

        // 延迟一小会儿汇报，确保 UI 状态更新完整
        QTimer::singleShot(100, this, [this](){
            // 汇报 NG 给 MainWindow
            emit testFinished(m_id, false, Reason_Common);
        });
    }
}
