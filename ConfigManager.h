#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QVector>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDebug>
#include <QFileInfo>
#include <QDir>

// --- 定义数据结构 ---
enum TestType { Type_Match, Type_Range, Type_Exist, Type_NotMatch ,Type_Display };

struct IdentityRule {
    QString key;
    QString name;
    QString prefix;
    bool enable;
};

struct TestRule {
    QString key;
    QString name;
    TestType type;
    QString targetVal;
    double minVal;
    double maxVal;
    bool enable;
};

struct PlcConfig {
    bool enabled;
    QString ip;
    int port;
};

// --- 配置管理器类 ---
class ConfigManager {
public:
    static ConfigManager& instance() {
        static ConfigManager instance;
        return instance;
    }

    // === 扫描 configs 文件夹下的所有 .json 文件 ===
    static QStringList getConfigFileList() {
        QDir dir("configs");
        QStringList filters;
        filters << "*.json";
        return dir.entryList(filters, QDir::Files | QDir::NoDotAndDotDot);
    }

    // 加载指定文件
    void loadConfig(const QString& fileName) {
        QString fullPath = QString("configs/%1").arg(fileName);

        QFile file(fullPath);
        // 兼容旧版本：如果子目录找不到，找根目录
        if (!file.exists()) {
            fullPath = fileName;
            file.setFileName(fullPath);
        }

        QFileInfo info(file);
        qDebug() << "正在加载配置:" << info.absoluteFilePath();

        if (!file.open(QIODevice::ReadOnly)) {
            qDebug() << "Config load failed: File not found.";
            return;
        }

        QByteArray data = file.readAll();
        file.close();

        // --- 智能编码修复 (Win7 GBK 兼容) ---
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(data, &error);

        if (doc.isNull() || error.error != QJsonParseError::NoError) {
            qDebug() << "UTF-8 解析失败，尝试使用 Local8Bit (GBK) ...";
            QString strGBK = QString::fromLocal8Bit(data);
            doc = QJsonDocument::fromJson(strGBK.toUtf8(), &error);
            if(doc.isNull()) {
                qDebug() << "JSON 格式严重错误，请检查 config.json";
                return;
            }
        }

        QJsonObject root = doc.object();

        // 【关键修改 1】 保存 JSON 对象到成员变量，以便 getPlcConfig 使用
        m_jsonObj = root;

        parseIdentityRules(root.value("identity_rules").toArray());
        parseTelemetryRules(root.value("telemetry_rules").toArray());

        qDebug() << "配置加载完成。ID规则数:" << m_identities.size()
                 << " 检测项数:" << m_telemetries.size();
    }

    QVector<IdentityRule> getIdentityRules() const { return m_identities; }
    QVector<TestRule> getTelemetryRules() const { return m_telemetries; }

    bool isSnVerificationEnabled() {
        if (m_jsonObj.contains("sn_verification")) {
            return m_jsonObj.value("sn_verification").toObject().value("enabled").toBool(false);
        }
        return false; // 默认不开启
    }

    // 【关键修改 2】 实现获取 PLC 配置的函数
    PlcConfig getPlcConfig() {
        PlcConfig config;

        // 默认值 (如果配置文件没写，就用这个防止报错)
        config.enabled = true;
        config.ip = "192.168.1.91";
        config.port = 3050;

        // 尝试从 JSON 读取
        if (m_jsonObj.contains("plc_automation")) {
            QJsonObject plcObj = m_jsonObj.value("plc_automation").toObject();

            // 使用读取到的值，如果某个字段缺失则维持默认值
            if(plcObj.contains("enabled")) config.enabled = plcObj.value("enabled").toBool();
            if(plcObj.contains("ip"))      config.ip      = plcObj.value("ip").toString();
            if(plcObj.contains("port"))    config.port    = plcObj.value("port").toInt();

            qDebug() << "PLC 配置已加载 -> IP:" << config.ip << " Port:" << config.port;
        } else {
            qWarning() << "未找到 plc_automation 配置段，使用默认产线地址: 192.168.1.91:3050";
        }

        return config;
    }

    int getTestTimeout() {
        if (m_jsonObj.contains("plc_automation")) {
            // 默认 15000 毫秒 (15秒)
            return m_jsonObj.value("plc_automation").toObject().value("test_timeout").toInt(15000);
        }
        return 15000;
    }

private:
    QVector<IdentityRule> m_identities;
    QVector<TestRule> m_telemetries;

    // 【关键修改 3】 新增成员变量，存储完整的 JSON 对象
    QJsonObject m_jsonObj;

    void parseIdentityRules(const QJsonArray& arr) {
        m_identities.clear();
        for (const auto& val : arr) {
            QJsonObject obj = val.toObject();
            if (!obj.value("enable").toBool(true)) continue;
            m_identities.append({
                obj.value("key").toString(),
                obj.value("name").toString(),
                obj.value("prefix").toString(),
                true
            });
        }
    }

    void parseTelemetryRules(const QJsonArray& arr) {
        m_telemetries.clear();
        for (const auto& val : arr) {
            QJsonObject obj = val.toObject();
            if (!obj.value("enable").toBool(true)) continue;

            TestRule rule;
            rule.key = obj.value("key").toString();
            rule.name = obj.value("name").toString();
            rule.enable = true;

            QString typeStr = obj.value("type").toString();
            if (typeStr == "range") {
                rule.type = Type_Range;
                rule.minVal = obj.value("min").toDouble(-9999);
                rule.maxVal = obj.value("max").toDouble(9999);
            }
            else if (typeStr == "not_match" || typeStr == "!=") {
                rule.type = Type_NotMatch;
                rule.targetVal = obj.value("target").toString();
                if(rule.targetVal.isEmpty() && obj.contains("target"))
                    rule.targetVal = QString::number(obj.value("target").toDouble());
            }
            else if (typeStr == "display" || typeStr == "show" || typeStr == "read") {
                rule.type = Type_Display;
            }
            else {
                rule.type = Type_Match;
                rule.targetVal = obj.value("target").toString();
                if(rule.targetVal.isEmpty() && obj.contains("target"))
                    rule.targetVal = QString::number(obj.value("target").toDouble());
            }
            m_telemetries.append(rule);
        }
    }

    // 私有构造，单例模式
    ConfigManager() {}
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
};

#endif // CONFIGMANAGER_H
