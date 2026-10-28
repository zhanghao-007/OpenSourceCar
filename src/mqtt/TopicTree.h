#ifndef TOPICTREE_H
#define TOPICTREE_H

#include "MqttTypes.h"

#include <QHash>
#include <QList>
#include <QMutex>
#include <QStringList>

namespace Mqtt {

// 一次主题匹配的结果：投递给哪个客户端、用哪个 QoS、命中了哪个过滤器
struct MatchResult {
    QString clientId;
    QoS     qos     = QoS0;
    QString filter;

    MatchResult() = default;
    MatchResult(const QString &c, QoS q, const QString &f)
        : clientId(c), qos(q), filter(f) {}
};

// ---------------------------------------------------------------------------
// TopicTree —— 订阅关系表
//
// 实现取舍：小车后台的订阅规模通常在数百到数千条，这里用
// "过滤器 -> {客户端 -> QoS}" 的扁平表 + 线性匹配，而不是 trie 树。
// 好处是代码短、无递归、无指针生命周期问题，且能完整支持 '+' / '#' 与
// "$share" 共享订阅前缀；千级订阅下每次匹配耗时在微秒量级，完全够用。
// 所有公开接口加锁，可被网络线程与 UI 线程并发调用。
// ---------------------------------------------------------------------------
class TopicTree
{
public:
    TopicTree();

    // 订阅：同一客户端重复订阅同一过滤器时覆盖 QoS
    void subscribe(const QString &clientId, const QString &filter, QoS qos);

    // 退订：不传 filter 时退订该客户端的全部主题
    void unsubscribe(const QString &clientId, const QString &filter = QString());

    // 客户端断开后的清理
    void removeClient(const QString &clientId);

    // 把一条 PUBLISH 路由出去，返回所有命中项
    QList<MatchResult> match(const QString &topic) const;

    // 查询某客户端的全部订阅（UI 展示用）
    QList<Subscription> subscriptionsOf(const QString &clientId) const;

    // 过滤器的订阅者数量（UI 展示用）
    int subscriberCount(const QString &filter) const;

    QStringList allFilters() const;
    int  subscriptionCount() const;
    void clear();

private:
    // filter -> (clientId -> qos)
    QHash<QString, QHash<QString, QoS> > m_table;
    mutable QMutex m_mutex;
};

// ---------------------------------------------------------------------------
// 主题层级树节点（UI 主题树控件的数据源）
// ---------------------------------------------------------------------------
struct TopicNode {
    QString                        segment;   // 本层名称，根节点为空
    QString                        fullPath;  // 从根到本层的完整路径
    int                            messageCount = 0;
    int                            subscriberCount = 0;
    QHash<QString, TopicNode *>    children;

    TopicNode() = default;
    ~TopicNode() { qDeleteAll(children); }
};

// 把一批主题名（"car/001/telemetry"）组织成树，root 由调用方持有并在用完后 delete
TopicNode *buildTopicTree(const QStringList &topics,
                          const QHash<QString, int> &counts,
                          const QHash<QString, int> &subscribers);

} // namespace Mqtt

#endif // TOPICTREE_H
