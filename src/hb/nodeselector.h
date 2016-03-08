#ifndef NODESELECTOR_H
#define NODESELECTOR_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>
#include <QVariantMap>

#include "pidiscoverer.h"

class NodeSelector : public QObject
{
    Q_OBJECT
public:
    static NodeSelector* instance(QNetworkAccessManager *nam = 0);
    QString deviceAddress(const PiNode& node) const;
    PiDiscoverer* discoverer() const { return m_discoverer; }
    QString videoUriForCurrentNode();

Q_SIGNALS:
    void thermalUrl(const QUrl &thermalUrl);

public Q_SLOTS:
    void onNewNodeDiscovered(const PiNode& node);
    bool selectNext();
    bool selectPrevious();
    PiNode currentNode() const;
    int currentNodeIndex() const { return m_currentIndex; }
    // returns port on the client side where udp packets are being sents
    int startStreaming(const PiNode& node, const QString& optionsString);
    int startStreaming(int nodeIndex, const QString& optionsString);
    void stopStreaming(int nodeIndex);
    bool startThermal(int nodeIndex);
    bool startThermal(const PiNode& node);
    void terminatePicam(const PiNode& node);
    void terminateThermal(int index);
    void terminateThermal(const PiNode& node);
    void shutdownAll();
    void restartAll();
    // mavproxy and thermal start automatically if the node is capable

private Q_SLOTS:
    void replyFinished();

private:
    explicit NodeSelector(QNetworkAccessManager *nam = 0, QObject *parent = 0);
    virtual ~NodeSelector();
    void sendRequest(const QUrl &url, const QVariantMap &properties = QVariantMap());
    void terminateMavProxy(const PiNode& node);

    PiDiscoverer            *m_discoverer;
    QNetworkAccessManager   *m_nam;
    int                      m_currentIndex;
};

#endif // NODESELECTOR_H
