#include <QCoreApplication>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QTextStream>
#include <QDebug>
#include <QDebugStateSaver>

namespace {
Q_LOGGING_CATEGORY(appCategory, "myapp.app")
Q_LOGGING_CATEGORY(netCategory, "myapp.net")

class MyClass {
public:
    MyClass(int id, QString name) : id_(id), name_(name) {}
    int id() const {return id_;}
    const QString &name() const {return name_;}
private:
    int id_;
    QString name_;
};
QDebug operator<<(QDebug dbg, const MyClass &obj) {
    QDebugStateSaver stateSaver(dbg);
    return dbg.nospace().noquote() << QString("(%1, %2)").arg(obj.id()).arg(obj.name());
}

#ifdef DYNAMIC_LOG_SETTING
void logHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    const auto &message = qFormatLogMessage(type, context, msg);
    QTextStream cerr(stderr);
    cerr << message << endl;
    QFile file("dynamic_logging_setting.log");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        cerr << "Cannot open log file:" << file.fileName();
        return;
    }
    QTextStream(&file) << message << endl;
}
static const char * const serverName = "dynamic_logging_setting";
void logRoutine() {
    qInstallMessageHandler(logHandler);
    auto localServer = new QLocalServer(qApp);
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [localServer] {
        qCWarning(netCategory) << "Cleanup localserver.";
        localServer->close();
        QLocalServer::removeServer(serverName);
    });
    qCInfo(appCategory) << "Listen local socket...";
    if (localServer->listen(serverName)) {
        qCInfo(netCategory) << "opened.";
        QObject::connect(localServer, &QLocalServer::newConnection, localServer, [localServer] {
            auto socket = localServer->nextPendingConnection();
            if (!socket->waitForReadyRead(1000)) {
                qCWarning(netCategory) << "cannot read data.";
                return;
            } else {
                auto data = socket->readAll();
                auto args = data.split('\b');
                qCInfo(netCategory) << QString("%1 byte(s) received:").arg(data.size()) << args.join(' ');
                if (args.size() >= 2) {
                    const auto &command = args.at(0);
                    const auto &arg = args.at(1);
                    if (command == "-f") {
                        qSetMessagePattern(arg);
                        qCInfo(appCategory) << "Set log message format:" << arg;
                    } else if (command == "-r") {
                        QLoggingCategory::setFilterRules(QString::fromLocal8Bit(arg).replace(";", "\n"));
                        qCInfo(appCategory) << "Set log fileter rules" << arg;
                    } else if (command == "-q") {
                        QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
                        qCInfo(appCategory) << "Quit application:" << arg;
                    }
                }
            }
            socket->close();
            socket->deleteLater();
        });
    } else {
        qCInfo(appCategory) << "Cannot listen local socket, connect to server.";
        if (qApp->arguments().size() < 2) {
            qCInfo(appCategory) << "dynamic_logging_setting is already launched.";
            QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
            return;
        }
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (!socket.isWritable()) {
            qCWarning(netCategory) << "Cannot connect to server, cleanup.";
            QLocalServer::removeServer(serverName);
            QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
            return;
        }
        qCInfo(netCategory) << "connected.";
        auto args = qApp->arguments();
        args.takeFirst();
        auto data = args.join('\b').toLocal8Bit();
        socket.write(data);
        if (socket.waitForBytesWritten(1000)) {
            qCInfo(netCategory) << QString("send %1 byte(s): ").arg(data.size()) << data;
        } else {
            qCWarning(netCategory) << "cannot send data.";
        }
        QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
    }
}
Q_COREAPP_STARTUP_FUNCTION(logRoutine)
#endif
}

#include <QTimer>
#include <csignal>
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QTimer timer;
    MyClass obj(1234, "object_1234");
    QObject::connect(&timer, &QTimer::timeout, &app, [&obj] {
        qCDebug(appCategory) << "Some debug logs." << obj;
    });
    auto quit = [](int sig) {
        qCInfo(appCategory) << QString("Receive UNIX signal %1, quit.").arg(sig);
        qApp->quit();
    };
    for (auto s : {SIGINT, SIGTERM}) {
        std::signal(s, quit);
    }
    timer.start(1000);
    return app.exec();
}
