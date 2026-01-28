#include "SnManager.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QMutexLocker>

SnManager::SnManager(QObject *parent) : QObject(parent)
{
}

SnManager::~SnManager()
{
    clearData();
}

// 加载数据文件
bool SnManager::loadData(const QString &filePath)
{
    // 加锁，防止在加载过程中有线程来查表
    QMutexLocker locker(&m_mutex);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "SNManager: 无法打开文件" << filePath;
        return false;
    }

    // 先清空旧数据
    m_dataMap.clear();

    QTextStream in(&file);
    // 处理中文编码，如果CSV是GBK需改为 "GBK"，这里默认UTF-8
    in.setCodec("UTF-8");

    int loadedCount = 0;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        // 假设 CSV 格式为: IMSI,SN
        // 例如: 460113362357785,SN20260115001
        QStringList parts = line.split(',');

        if (parts.size() >= 2) {
            QString imsi = parts[0].trimmed();
            QString sn = parts[1].trimmed();

            // 存入哈希表
            m_dataMap.insert(imsi, sn);
            loadedCount++;
        }
        else if (parts.size() == 1) {
            // 兼容只有 IMSI 的白名单模式 (SN 为空字符串)
            QString imsi = parts[0].trimmed();
            m_dataMap.insert(imsi, "");
            loadedCount++;
        }
    }

    file.close();
    qDebug() << "SNManager: 数据加载完成，共" << loadedCount << "条";
    return true;
}

// 核心校验函数
bool SnManager::checkIdentity(const QString &inputCode, QString &outSn)
{
    // 加锁，保证读取安全性
    QMutexLocker locker(&m_mutex);

    if (m_dataMap.contains(inputCode)) {
        // 找到了！
        outSn = m_dataMap.value(inputCode);
        return true;
    }

    // 没找到
    outSn.clear();
    return false;
}

// 清空数据
void SnManager::clearData()
{
    QMutexLocker locker(&m_mutex);
    m_dataMap.clear();
}

// 获取数量
int SnManager::getDataCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_dataMap.size();
}
