#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>
#include <QGridLayout>
#include <QSpinBox>
#include <QLabel>
#include <QToolBar>
#include <QMessageBox>
#include <QKeyEvent>
#include "DeviceChannelWidget.h" // 引用你的通道组件头文件
#include "SnManager.h"
#include "PlcController.h"
#include "ConfigManager.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    // [修改] 使用事件过滤器代替 keyPressEvent
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    // [核心槽函数] 当通道数量设置改变时触发
    void onChannelCountChanged(int count);
    void onPlcStartSignal();
    void onPlcStopSignal(); // [新增]
    // 测试结果槽 [修改签名]
    void onChannelTestFinished(int channelId, bool isPass, int failureReason);
    // [新增] 处理通道上报的身份信息
    void onChannelIdentityReported(const QString &idValue);
    // 【新增】 用于接收通道发来的信号
    void onAnyChannelIdentityReceived();
    void appendToLog(const QString &msg);
    void tryLoadBarcode();
    void onBarcodePollTimeout(); // [新增] 用于轮询 SN.txt 是否生成


private:
    QWidget *m_centralWidget;      // 中心部件
    QGridLayout *m_gridLayout;     // 网格布局管理器

    // [核心容器] 存储当前所有活跃的通道对象指针
    QList<DeviceChannelWidget*> m_channels;

    // 顶部工具栏控件
    QSpinBox *m_spinBoxCount;      // 用于设置通道数量


    // [新增] 扫码枪输入缓存
    QString m_scanBuffer;

    // [新增] 扫码缓存池 (暂存还没有对应串口数据的条码)
    QStringList m_scanCache;

    // [新增] 核心控制器
    PlcController *m_plc;
    SnManager *m_snManager;

    // [新增] PLC 状态指示灯
    QLabel *m_lblPlcStatus;

    // [新增] 界面显示缓存池内容 (可选，方便工人看)
    QLabel *m_lblCacheStatus;

    // 【新增】 标记是否已经加载过 SN 文件
    bool m_hasLoadedSnFile;

    // [新增] 设置 PLC 状态样式的辅助函数
    // status: 0=禁用(灰), 1=断开(红), 2=连接(绿)
    void updatePlcStatusIndicator(int status);
    void loadInitialBarcodeFromFile();
    // [新增] 辅助函数：分发条码并启动 (无论有无条码都通过它启动)
    void distributeBarcodeAndStart(const QString &rawContent);

    void finalizePlcResult();
    void checkBarcodeTimeout();

    // [新增] 重试计数器
    int m_retryCount = 0;
    const int MAX_RETRIES = 3; // 最大重试次数
    const int RETRY_DELAY_MS = 200; // 每次重试间隔 200ms

    // [新增] 扫码文件等待定时器
    QTimer *m_barcodePollTimer;
    int m_pollRetryCount;
};

#endif // MAINWINDOW_H
