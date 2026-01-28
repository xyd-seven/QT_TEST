#ifndef DEVICECHANNELWIDGET_H
#define DEVICECHANNELWIDGET_H

#include <QWidget>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QTimer>
#include <QMap>
#include <QDateTime>
#include <QFile>
#include <QDir>

#include "ConfigManager.h"

// 前置声明
class SnManager;

// ==========================================
// [恢复] 手动扫码结果枚举
// ==========================================
enum class ScanResult {
    Match,      // 匹配成功 (Green)
    Mismatch,   // 就在这个通道里，但是对不上 (Red)
    Ignore      // 不是这个通道的事 (Pass)
};

// ==========================================
// [新增] PLC 测试失败原因枚举
// ==========================================
enum FailureReason {
    Reason_None = 0,    // PASS
    Reason_Common = 1,  // 超时或其他错误
    Reason_IMEI = 2     // 严重的 IMEI 不一致
};

class DeviceChannelWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceChannelWidget(int id, QWidget *parent = nullptr);
    ~DeviceChannelWidget();

    int id() const { return m_id; }
    // 获取失败原因
    int getFailureReason() const {
        if (!m_hasError) {
            return Reason_None; // 没有错误 -> PASS
        }
        if (m_isImeiMismatch) {
            return Reason_IMEI; // 现有的 IMEI 错误标志位为真 -> 严重错误
        }
        return Reason_Common;   // 有错误但不是IMEI错 -> 普通错误
    }

    // 查询测试状态
    bool isTesting() const { return m_isTesting; }
    // 获取是否有错误
    bool hasError() const { return m_hasError; }

    void resetUI(bool keepBarcode = false);
    //void startTest();
    void startTest(bool keepBarcode = false);
    void setBarcode(const QString &code);
    void setExpectedIdentity(const QString &key, const QString &value);
    // [新增] 外部调用的自动启动接口
    void startTestWithBarcode(const QString &sn);
    QString getBarcode() const;

    // ==========================================
    // [恢复] 手动扫码校验函数声明
    // ==========================================
    ScanResult checkScanInput(const QString &code);

signals:
    // 信号增加失败原因参数
    void testFinished(int channelId, bool isPass, int failureReason);
    void logMessage(const QString &msg);
    void identityReported(const QString &idValue);

private slots:
    void onSerialReadyRead();
    void onTestTimeout();

    // 按钮槽函数
    void onStartClicked();
    void onStopClicked();

    // 扫码框变化槽函数
    void onBarcodeChanged(const QString &text);

private:
    void setupUi();
    void processBuffer(); // 处理缓冲区
    void createLogFile(); // 创建日志文件

    void parseLine(const QString &line);
    void parseTelemetry(const QString &dataPart);

    void updateSerialDisplay();
    void updateResultItem(const QString &key, const QString &val);
    void performComparison();
    void setChannelStatus(bool active);

private:
    int m_id;
    bool m_isTesting = false;
    bool m_hasError = false;
    bool m_isImeiMismatch = false; // 专门记录 IMEI 错误

    // --- 硬件对象 ---
    QSerialPort *m_serial;
    QByteArray m_buffer;
    QTimer *m_testTimer;

    // 日志文件
    QFile *m_logFile = nullptr;

    // 数据容器
    QMap<QString, QString> m_currentIds;   // 读到的
    QMap<QString, QString> m_expectedIds;  // 期望的
    QMap<QString, int> m_mapResRow;        // 表格行索引
    qint64 m_lastResetTime;

    SnManager *m_snManager = nullptr;

    // --- UI 控件 ---
    QGroupBox *m_group;
    QComboBox *m_cbModel;
    QComboBox *m_cbPort;
    QComboBox *m_cbBaud;

    QLineEdit *m_editBarcode;
    QLineEdit *m_editSerialRead;
    QTableWidget *m_tableRes;
    QPlainTextEdit *m_logView;
};

#endif // DEVICECHANNELWIDGET_H
