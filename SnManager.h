#ifndef SNMANAGER_H
#define SNMANAGER_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QMutex>

/**
 * @brief SN 管理器类
 * * 职责：
 * 1. 从 CSV/TXT 文件加载 "IMSI - SN" 对应关系表。
 * 2. 提供线程安全的查表接口。
 * 3. 替代旧 MFC 代码中的二分查找算法 (getSn) 和硬编码数组。
 */
class SnManager : public QObject
{
    Q_OBJECT

public:
    explicit SnManager(QObject *parent = nullptr);
    ~SnManager();

    /**
     * @brief 加载 SN 数据表
     * @param filePath 数据文件的路径 (通常是 .csv 或 .txt)
     * 文件格式建议: IMSI,SN (一行一条)
     * @return true 加载成功, false 加载失败
     */
    bool loadData(const QString &filePath);

    /**
     * @brief 检查 IMSI/SN 是否合法
     * * 对应旧代码中的 getSn + imsi2sn 逻辑。
     * * @param inputCode 扫码枪扫入的内容 或 串口读到的 IMSI
     * @param outSn [输出] 如果找到对应的 SN，通过此参数传出
     * @return true 校验通过 (在表中找到了), false 校验失败 (非法设备)
     */
    bool checkIdentity(const QString &inputCode, QString &outSn);

    /**
     * @brief 清空当前内存中的数据
     * 用于切换产品或重新加载配置时
     */
    void clearData();

    /**
     * @brief 获取当前加载的数据条数
     */
    int getDataCount() const;

private:
    // 内存数据库：使用哈希表存储，查询速度为 O(1)，远快于二分查找
    // Key: IMSI (作为唯一标识)
    // Value: SN (对应的序列号)
    QHash<QString, QString> m_dataMap;

    // 读写锁：防止在测试过程中重新加载文件导致崩溃
    mutable QMutex m_mutex;
};

#endif // SNMANAGER_H
