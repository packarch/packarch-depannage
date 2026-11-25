#include "chatfile.hpp"
#include "session.hpp"

#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestDepannage : public QObject {
    Q_OBJECT
private slots:
    void statusJsonRoundTrip() {
        session::Status s;
        s.active = true;
        s.end = 10;
        s.hostname = u"packarch-pc"_s;
        s.helperName = u"Fabrice"_s;
        const QJsonObject json = s.toJson();
        QCOMPARE(session::Status::fromJson(json), s);
        // Les noms lus par les anciennes versions et par la compétence Claude
        for (const char* key : {"active", "end", "hostname", "backend", "auth_url", "dns_name", "ssh_sessions",
                                "helper", "last_activity", "helper_name", "helper_login"})
            QVERIFY2(json.contains(QLatin1StringView(key)), key);
        QVERIFY(json.value("auth_url"_L1).isNull());
    }

    void peers() {
        const QJsonObject j = QJsonDocument::fromJson(R"({"Peer": {
            "a": {"HostName": "unraid", "RxBytes": 0, "LastWrite": "0001-01-01T00:00:00Z", "UserID": 7},
            "b": {"HostName": "archlinux", "RxBytes": 600, "LastWrite": "2026-11-01T12:06:30.469348563+01:00", "UserID": 7},
            "c": {"HostName": "minipc", "RxBytes": 10, "LastWrite": "2026-11-01T10:00:00Z", "UserID": 8}
        }, "User": {"7": {"LoginName": "fab@example.com", "DisplayName": "Fabrice"}}})").object();
        const session::Peer expected{u"archlinux"_s, 1793531190, u"Fabrice"_s, u"fab@example.com"_s};
        QCOMPARE(session::lastPeer(j).value(), expected);
        QVERIFY(!session::unixTime(u"0001-01-01T00:00:00Z"_s).has_value());
        QCOMPARE(session::unixTime(u"2026-11-01T10:00:00Z"_s).value(), 1793527200);
    }

    void machineNames() {
        QCOMPARE(session::machineName(u"PC-Maman\n"_s), u"packarch-pc-maman"_s);
        QCOMPARE(session::machineName(u"Salon de Mamie"_s), u"packarch-salon-de-mamie"_s);
        QCOMPARE(session::machineName(u"packarch"_s), u"packarch"_s);
        QCOMPARE(session::machineName(u"  \n"_s), u"packarch-ordinateur"_s);
        QCOMPARE(session::machineName(u"éé"_s), u"packarch-ordinateur"_s);
    }

    void connectionKeys() {
        QVERIFY(session::validKey(u"tskey-auth-kAbc123CNTRL-xYz789"_s));
        QVERIFY(!session::validKey(u""_s));
        QVERIFY(!session::validKey(u"tskey-client-kAbc123CNTRL-xYz789"_s));
        QVERIFY(!session::validKey(u"tskey-auth-kAbc CNTRL-x"_s));
        QVERIFY(!session::validKey(u"tskey-auth-a-b\n--timeout=0"_s));
        QVERIFY(!session::validKey(u"file:/etc/shadow"_s));
        QVERIFY(!session::validKey(u"tskey-auth-a-"_s + QString(300, u'b')));
    }

    void cleanedText() {
        QCOMPARE(chat::clean(u"  bonjour\x1b[31m\ttoi\u009b\n "_s), u"bonjour[31m toi"_s);
        QCOMPARE(chat::clean(u"a\nb"_s), u"a\nb"_s);
        QCOMPARE(chat::clean(QString(chat::MAX_CHARS + 5, u'é')).size(), chat::MAX_CHARS);
    }

    void invalidLinesIgnored() {
        QVERIFY(!chat::parseLine(""));
        QVERIFY(!chat::parseLine("{pas du json"));
        QVERIFY(!chat::parseLine(R"({"from":"root","time":1,"text":"x"})"));
        QVERIFY(!chat::parseLine(R"({"from":"user","time":1,"text":" \u001b "})"));
        const ChatMessage expected{false, 1, u"ok"_s};
        QCOMPARE(chat::parseLine(R"({"from":"helper","time":1,"text":"ok"})").value(), expected);
        // Le format écrit est celui qui est lu
        QCOMPARE(chat::parseLine(chat::toLine({true, 5, u"ça va ?"_s}).trimmed()).value(), (ChatMessage{true, 5, u"ça va ?"_s}));
    }

    void notificationFitsTwoLines() {
        QCOMPARE(chat::notificationBody(u"  a\nb  c "_s), u"a b c"_s);
        QCOMPARE(chat::notificationBody(QString(36, u'é')), QString(36, u'é'));
        QCOMPARE(chat::notificationBody(QString(37, u'é')), QString(36, u'é') + u'…');
        QCOMPARE(chat::notificationBody(u"a <b> & c"_s), u"a &lt;b&gt; &amp; c"_s);
    }

    void appendedThenReadOnce() {
        QTemporaryDir dir;
        const QString p = dir.filePath(u"chat.jsonl"_s);
        const auto write = [&p](const QByteArray& data, QIODevice::OpenMode mode) {
            QFile f(p);
            QVERIFY(f.open(mode));
            f.write(data);
        };
        write("", QIODevice::WriteOnly);
        qint64 pos = 0;
        QVERIFY(chatfile::appendTo(p, true, u"bonjour\nça va ?"_s).isEmpty());
        QVERIFY(chatfile::appendTo(p, true, u"   "_s).isEmpty());
        const auto got = chatfile::readNew(p, pos);
        QCOMPARE(got.size(), 1);
        QCOMPARE(got[0].text, u"bonjour\nça va ?"_s);
        QVERIFY(got[0].fromUser);
        QVERIFY(chatfile::readNew(p, pos).isEmpty());
        // Ligne en cours d'écriture : attendue jusqu'à sa fin
        write(R"({"from":"helper",)", QIODevice::Append);
        QVERIFY(chatfile::readNew(p, pos).isEmpty());
        write("\"time\":1,\"text\":\"oui\"}\n", QIODevice::Append);
        QCOMPARE(chatfile::readNew(p, pos).value(0).text, u"oui"_s);
        // Fichier recréé (nouvelle session) : relu depuis le début
        write("", QIODevice::WriteOnly);
        QVERIFY(chatfile::appendTo(p, true, u"x"_s).isEmpty());
        QCOMPARE(chatfile::readNew(p, pos).value(0).text, u"x"_s);
        // Sans fichier (pas de session) : une erreur, rien de créé
        QVERIFY(!chatfile::appendTo(dir.filePath(u"absent"_s), true, u"x"_s).isEmpty());
        QVERIFY(!QFile::exists(dir.filePath(u"absent"_s)));
    }
};

QTEST_APPLESS_MAIN(TestDepannage)
#include "test_depannage.moc"
