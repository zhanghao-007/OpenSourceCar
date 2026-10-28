#include "TopicTree.h"

#include <QStringList>

namespace Mqtt {

// ---------------------------------------------------------------------------
// MqttTypes.h 中声明的主题工具函数
// ---------------------------------------------------------------------------

QString stripSharePrefix(const QString &filter)
{
    // 共享订阅："$share/<group>/<realFilter>"
    if (!filter.startsWith(QStringLiteral("$share/")))
        return filter;
    int idx = filter.indexOf(QLatin1Char('/'), 7);   // 跳过 "$share/"
    if (idx < 0)
        return filter;
    return filter.mid(idx + 1);
}

bool isValidTopicName(const QString &topic, bool allowWildcard)
{
    if (topic.isEmpty() || topic.length() > MAX_CLIENT_ID_LEN)
        return false;
    if (topic.contains(QChar(0x0000)))
        return false;

    const QStringList levels = topic.split(QLatin1Char('/'));
    for (int i = 0; i < levels.size(); ++i) {
        const QString &lv = levels.at(i);
        if (lv.contains(QLatin1Char('+')) || lv.contains(QLatin1Char('#'))) {
            if (!allowWildcard)
                return false;
            // 通配符必须独占一个层级
            if (lv == QStringLiteral("+"))
                continue;
            if (lv == QStringLiteral("#"))
                return i == levels.size() - 1;   // '#' 只能出现在最后一层
            return false;
        }
    }
    return true;
}

bool topicFilterMatch(const QString &rawFilter, const QString &topic)
{
    const QString filter = stripSharePrefix(rawFilter);
    if (filter.isEmpty() || topic.isEmpty())
        return false;

    // 空过滤器只匹配空主题
    if (filter == topic)
        return true;

    const QStringList fLevels = filter.split(QLatin1Char('/'));
    const QStringList tLevels = topic.split(QLatin1Char('/'));

    int ti = 0;
    for (int fi = 0; fi < fLevels.size(); ++fi) {
        const QString &f = fLevels.at(fi);

        if (f == QStringLiteral("#"))
            return true;   // '#' 匹配剩下的所有层级，也匹配零层（"sport/#" 匹配 "sport"）

        if (ti >= tLevels.size())
            return false;  // 过滤器还有层级，主题已耗尽

        const QString &t = tLevels.at(ti);

        // MQTT-4.7.2：以 '$' 开头的主题不参与顶层通配符匹配
        if (fi == 0 && ti == 0 && t.startsWith(QLatin1Char('$'))
                && (f == QStringLiteral("+") || f == QStringLiteral("#"))) {
            return false;
        }

        if (f == QStringLiteral("+")) {
            ++ti;
            continue;      // 单层通配符，匹配任意非空/空层级
        }
        if (f != t)
            return false;
        ++ti;
    }

    // 过滤器层级已走完：只有当主题的层级也恰好耗尽时才算匹配
    // （filter="a/b" 不应匹配 topic="a/b/c"）
    return ti == tLevels.size();
}

// ---------------------------------------------------------------------------
// TopicTree
// ---------------------------------------------------------------------------

TopicTree::TopicTree()
{
}

void TopicTree::subscribe(const QString &clientId, const QString &filter, QoS qos)
{
    QMutexLocker locker(&m_mutex);
    m_table[filter].insert(clientId, qos);
}

void TopicTree::unsubscribe(const QString &clientId, const QString &filter)
{
    QMutexLocker locker(&m_mutex);
    if (filter.isEmpty()) {
        // 退订该客户端的全部主题
        QMutableHashIterator<QString, QHash<QString, QoS> > it(m_table);
        while (it.hasNext()) {
            it.next();
            it.value().remove(clientId);
            if (it.value().isEmpty())
                it.remove();
        }
        return;
    }
    QHash<QString, QHash<QString, QoS> >::iterator it = m_table.find(filter);
    if (it == m_table.end())
        return;
    it.value().remove(clientId);
    if (it.value().isEmpty())
        m_table.erase(it);
}

void TopicTree::removeClient(const QString &clientId)
{
    unsubscribe(clientId);
}

QList<MatchResult> TopicTree::match(const QString &topic) const
{
    QList<MatchResult> result;
    QMutexLocker locker(&m_mutex);

    QHashIterator<QString, QHash<QString, QoS> > it(m_table);
    while (it.hasNext()) {
        it.next();
        if (!topicFilterMatch(it.key(), topic))
            continue;
        const QHash<QString, QoS> &subs = it.value();
        QHashIterator<QString, QoS> sit(subs);
        while (sit.hasNext()) {
            sit.next();
            result.append(MatchResult(sit.key(), sit.value(), it.key()));
        }
    }
    return result;
}

QList<Subscription> TopicTree::subscriptionsOf(const QString &clientId) const
{
    QList<Subscription> list;
    QMutexLocker locker(&m_mutex);

    QHashIterator<QString, QHash<QString, QoS> > it(m_table);
    while (it.hasNext()) {
        it.next();
        QHash<QString, QoS>::const_iterator sit = it.value().constFind(clientId);
        if (sit != it.value().constEnd())
            list.append(Subscription(it.key(), sit.value()));
    }
    return list;
}

int TopicTree::subscriberCount(const QString &filter) const
{
    QMutexLocker locker(&m_mutex);
    QHash<QString, QHash<QString, QoS> >::const_iterator it = m_table.constFind(filter);
    return (it == m_table.constEnd()) ? 0 : it.value().size();
}

QStringList TopicTree::allFilters() const
{
    QMutexLocker locker(&m_mutex);
    return m_table.keys();
}

int TopicTree::subscriptionCount() const
{
    QMutexLocker locker(&m_mutex);
    int n = 0;
    QHashIterator<QString, QHash<QString, QoS> > it(m_table);
    while (it.hasNext()) {
        it.next();
        n += it.value().size();
    }
    return n;
}

void TopicTree::clear()
{
    QMutexLocker locker(&m_mutex);
    m_table.clear();
}

// ---------------------------------------------------------------------------
// UI 主题树构建
// ---------------------------------------------------------------------------

static TopicNode *makeNode(const QString &segment, const QString &fullPath)
{
    TopicNode *n = new TopicNode();
    n->segment   = segment;
    n->fullPath  = fullPath;
    return n;
}

TopicNode *buildTopicTree(const QStringList &topics,
                          const QHash<QString, int> &counts,
                          const QHash<QString, int> &subscribers)
{
    TopicNode *root = makeNode(QString(), QString());

    foreach (const QString &topic, topics) {
        if (topic.isEmpty())
            continue;
        const QStringList levels = topic.split(QLatin1Char('/'));
        TopicNode *cur = root;
        QString path;
        for (int i = 0; i < levels.size(); ++i) {
            const QString &lv = levels.at(i);
            path = path.isEmpty() ? lv : path + QLatin1Char('/') + lv;
            QHash<QString, TopicNode *>::iterator it = cur->children.find(lv);
            if (it == cur->children.end())
                it = cur->children.insert(lv, makeNode(lv, path));
            cur = it.value();
        }
        cur->messageCount    = counts.value(topic, 0);
        cur->subscriberCount = subscribers.value(topic, 0);
    }
    return root;
}

} // namespace Mqtt
