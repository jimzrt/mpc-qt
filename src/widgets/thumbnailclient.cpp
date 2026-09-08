#include <QApplication>
#include <QDir>
#include <QMetaObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QThread>
#include "logger.h"
#include "thumbnailclient.h"

static constexpr char logModule[] = "thumbnailclient";

static QString normalizeWindowsPath(QString filename, [[maybe_unused]] bool isLocalFile)
{
#ifdef Q_OS_WINDOWS
    if (!isLocalFile)
        return filename;

    filename = QDir::toNativeSeparators(filename);
    if (filename.size() >= 260 && !filename.startsWith(u"\\\\?\\")) {
        if (filename.startsWith(u"\\\\"))
            filename = u"\\\\?\\UNC\\" + filename.mid(2);
        else
            filename.prepend(u"\\\\?\\");
    }
#endif
    return filename;
}

ThumbnailClient::ThumbnailClient(QObject *parent) :
    QObject(parent)
{
    worker = new QThread();
    worker->start();
    ctrl = new MpvController();
    ctrl->moveToThread(worker);

    connect(this, &ThumbnailClient::ctrlCommand,
            ctrl, &MpvController::command, Qt::QueuedConnection);
    connect(this, &ThumbnailClient::ctrlSetProperty,
            ctrl, &MpvController::setPropertyVariant, Qt::QueuedConnection);
    connect(ctrl, &MpvController::mpvPropertyChanged,
            this, &ThumbnailClient::onPropertyChanged, Qt::QueuedConnection);
    connect(ctrl, &MpvController::unhandledMpvEvent,
            this, &ThumbnailClient::onUnhandledEvent, Qt::QueuedConnection);
    connect(ctrl, &MpvController::logMessageByParts,
            Logger::singleton(), &Logger::makeLogDescriptively);
    connect(worker, &QThread::finished, ctrl, &QObject::deleteLater);

    MpvController::OptionList earlyOptions = {
        { "vo", "libmpv" },
        { "config", "no" },
        { "load-scripts", false },
        { "osc", "no" },
        { "osd-level", 0 },
        { "idle", "yes" },
        { "force-window", "no" },
        { "pause", true },
        { "keep-open", true },
        { "hr-seek", "no" },
        { "audio", "no" },
        { "audio-display", "no" },
        { "sub", "no" },
        { "ytdl", "yes" },
        { "ytdl-format", "bestvideo/best" },
        { "input-default-bindings", "no" },
        { "title", "mpc-qt-thumbnail" },
    };
    QMetaObject::invokeMethod(ctrl, [this, earlyOptions]() {
        this->ctrl->create(earlyOptions);
    }, Qt::BlockingQueuedConnection);

    MpvController::PropertyList properties = {
        { "video-params/aspect", 0, MPV_FORMAT_DOUBLE },
    };
    QMetaObject::invokeMethod(ctrl, [this, properties]() {
        this->ctrl->observeProperties(properties);
    }, Qt::QueuedConnection);

    QMetaObject::invokeMethod(ctrl, [this]() {
        this->ctrl->setLogLevel("warn");
    }, Qt::QueuedConnection);
}

ThumbnailClient::~ThumbnailClient()
{
    Logger::log(logModule, "~ThumbnailClient");
    if (ctrl) {
        QMetaObject::invokeMethod(ctrl, [this]() {
            this->ctrl->stop();
        }, Qt::BlockingQueuedConnection);
        ctrl = nullptr;
    }
    worker->quit();
    worker->wait();
    worker->deleteLater();
}

MpvController *ThumbnailClient::controller() const
{
    return ctrl;
}

void ThumbnailClient::openUrl(const QUrl &url)
{
    if (url.isEmpty())
        return;
    QString path = url.isLocalFile()
            ? normalizeWindowsPath(url.toLocalFile(), true)
            : QUrl::fromPercentEncoding(url.toEncoded());
    emit ctrlCommand(QStringList({"loadfile", path}));
}

void ThumbnailClient::seek(double time)
{
    emit ctrlCommand(QVariantList({"seek", time, QString("absolute"), QString("keyframes")}));
}

void ThumbnailClient::setOption(const QString &name, const QVariant &value)
{
    emit ctrlSetProperty(name, value);
}

void ThumbnailClient::onPropertyChanged(const QString &name, const QVariant &value,
                                        uint64_t userData)
{
    Q_UNUSED(userData)
    if (name == QLatin1String("video-params/aspect")) {
        double aspect = value.toDouble();
        if (aspect > 0)
            emit aspectChanged(aspect);
    }
}

void ThumbnailClient::onUnhandledEvent(int eventId)
{
    if (eventId == MPV_EVENT_FILE_LOADED)
        emit fileLoaded();
}

//----------------------------------------------------------------------------

ThumbnailGlWidget::ThumbnailGlWidget(MpvController *controller, QWidget *parent) :
    QOpenGLWidget(parent),
    ctrl(controller)
{
    connect(this, &QOpenGLWidget::frameSwapped,
            this, &ThumbnailGlWidget::self_frameSwapped);
}

ThumbnailGlWidget::~ThumbnailGlWidget()
{
    makeCurrent();
    if (render && ctrl) {
        ctrl->destroyRenderContext(render);
        render = nullptr;
    }
    doneCurrent();
}

void ThumbnailGlWidget::setHasVideo(bool yes)
{
    hasVideo = yes;
    update();
}

void ThumbnailGlWidget::initializeGL()
{
    initializeOpenGLFunctions();
#if MPV_CLIENT_API_VERSION < MPV_MAKE_VERSION(2,0)
    mpv_opengl_init_params glInit { &MpvGlWidget::get_proc_address, this, nullptr };
#else
    mpv_opengl_init_params glInit { &MpvGlWidget::get_proc_address, this };
#endif
    mpv_render_param params[] {
        { MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL) },
        { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit },
        { MPV_RENDER_PARAM_INVALID, nullptr },
        { MPV_RENDER_PARAM_INVALID, nullptr }
    };
    QWidget *nativeParent = nativeParentWidget();
    if (nativeParent == nullptr) {
        Logger::log(logModule, "no native parent handle");
    }
#if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
    else if (auto x11App = qApp->nativeInterface<QNativeInterface::QX11Application>()) {
        params[2].type = MPV_RENDER_PARAM_X11_DISPLAY;
        params[2].data = x11App->display();
    }
#if QT_VERSION >= QT_VERSION_CHECK(6,5,0)
    else if (auto wlApp = qApp->nativeInterface<QNativeInterface::QWaylandApplication>()) {
        params[2].type = MPV_RENDER_PARAM_WL_DISPLAY;
        params[2].data = wlApp->display();
    }
#endif
#endif
    render = ctrl->createRenderContext(params);
    mpv_render_context_set_update_callback(render, ThumbnailGlWidget::render_update, this);
}

void ThumbnailGlWidget::paintGL()
{
    if (!hasVideo || !render) {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    int yes = 1;
    mpv_opengl_fbo fbo { static_cast<int>(defaultFramebufferObject()), glWidth, glHeight, 0 };
    mpv_render_param params[] {
        { MPV_RENDER_PARAM_OPENGL_FBO, &fbo },
        { MPV_RENDER_PARAM_FLIP_Y, &yes },
        { MPV_RENDER_PARAM_INVALID, nullptr }
    };
    mpv_render_context_render(render, params);
}

void ThumbnailGlWidget::resizeGL(int w, int h)
{
    qreal r = devicePixelRatioF();
    glWidth = qRound(w * r);
    glHeight = qRound(h * r);
}

void ThumbnailGlWidget::render_update(void *ctx)
{
    auto widget = static_cast<ThumbnailGlWidget*>(ctx);
    QMetaObject::invokeMethod(widget, [widget]() {
        widget->maybeUpdate();
    });
}

void ThumbnailGlWidget::maybeUpdate()
{
    if (window()->isMinimized()) {
        makeCurrent();
        paintGL();
        context()->swapBuffers(context()->surface());
        self_frameSwapped();
        doneCurrent();
    } else {
        update();
    }
}

void ThumbnailGlWidget::self_frameSwapped()
{
    if (render && hasVideo)
        mpv_render_context_report_swap(render);
}
