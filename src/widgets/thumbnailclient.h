#ifndef THUMBNAILCLIENT_H
#define THUMBNAILCLIENT_H

#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QUrl>
#include "mpvwidget.h"

class QThread;

// Lean libmpv client for seekbar thumbnails. No scripts, UI properties, or input.
class ThumbnailClient : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailClient(QObject *parent = nullptr);
    ~ThumbnailClient();

    MpvController *controller() const;

public slots:
    void openUrl(const QUrl &url);
    void seek(double time);
    void setOption(const QString &name, const QVariant &value);

signals:
    void aspectChanged(double aspect);
    void fileLoaded();
    void ctrlCommand(const QVariant &params);
    void ctrlSetProperty(const QString &name, const QVariant &value);

private:
    void onPropertyChanged(const QString &name, const QVariant &value, uint64_t userData);
    void onUnhandledEvent(int eventId);

    QThread *worker = nullptr;
    MpvController *ctrl = nullptr;
};

class ThumbnailGlWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit ThumbnailGlWidget(MpvController *controller, QWidget *parent = nullptr);
    ~ThumbnailGlWidget();
    void setHasVideo(bool yes);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

private:
    static void render_update(void *ctx);
    void maybeUpdate();
    void self_frameSwapped();

    static constexpr char logModule[] = "thumbgl";
    MpvController *ctrl = nullptr;
    mpv_render_context *render = nullptr;
    int glWidth = 0;
    int glHeight = 0;
    bool hasVideo = false;
};

#endif // THUMBNAILCLIENT_H
