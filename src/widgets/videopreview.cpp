#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QPixmap>
#include <cmath>
#include "videopreview.h"

static constexpr char logModule[] =  "videopreview";
static constexpr int previewMarginX = 20;
static constexpr int labelHeight = 20;
static constexpr int shadowMargin = 8;
static constexpr int debounceMsec = 25;
static constexpr int cacheLimit = 64;
static constexpr double cacheBucketSec = 1.0;

VideoPreview::VideoPreview(QWidget *parent) : QWidget(parent)
{
    client = new ThumbnailClient(this);
    videoContainer = new QWidget(parent);
    auto *shadow = new QGraphicsDropShadowEffect(videoContainer);
    shadow->setBlurRadius(40);
    shadow->setOffset(0);
    shadow->setColor(QColor(0, 0, 0));
    videoContainer->setGraphicsEffect(shadow);

    videoWidget = new ThumbnailGlWidget(client->controller(), videoContainer);
    imageLabel = new QLabel(videoContainer);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->hide();
    textLabel = new QLabel(videoContainer);
    textLabel->setAlignment(Qt::AlignCenter);

    textLabel->setAutoFillBackground(true);
    updatePalette();

    connect(client, &ThumbnailClient::aspectChanged,
            this, &VideoPreview::updateWidth);
    connect(client, &ThumbnailClient::fileLoaded, this, [this]() {
        videoWidget->setHasVideo(true);
    });
    connect(videoWidget, &QOpenGLWidget::frameSwapped,
            this, &VideoPreview::onFrameSwapped);

    debounce.setSingleShot(true);
    debounce.setInterval(debounceMsec);
    connect(&debounce, &QTimer::timeout, this, &VideoPreview::requestThumbnail);

    shouldBeShown = false;
    hide();
    this->setVisible(false);
}

VideoPreview::~VideoPreview()
{
    debounce.stop();
    delete videoWidget;
    videoWidget = nullptr;
    delete client;
    client = nullptr;
}

void VideoPreview::openFile(const QUrl &fileUrl)
{
    if (fileUrl.isEmpty() || !client)
        return;
    if (videoWidget)
        videoWidget->setHasVideo(false);
    client->openUrl(fileUrl);
    aspectRatioSet = false;
    aspectRatio = 0;
    cache.clear();
    lastPixelHeight = 0;
    lastSeekTime = -1;
    grabPending = false;
    imageLabel->hide();
}

void VideoPreview::updatePalette()
{
    QPalette palette = QApplication::palette();
    palette.setColor(QPalette::Window, palette.color(QPalette::ToolTipBase));
    palette.setColor(QPalette::WindowText, palette.color(QPalette::ToolTipText));
    textLabel->setPalette(palette);
}

void VideoPreview::show(const QString &text, double videoPosition, const QPoint &where,
                        int mainWindowWidth, int previewHeight)
{
    textLabel->setText(text);
    pendingTime = videoPosition;

    int pixelHeight = qMax(2, int(std::round(previewHeight * devicePixelRatioF())));
    if (pixelHeight != lastPixelHeight) {
        lastPixelHeight = pixelHeight;
        cache.clear();
        videoWidget->setFixedHeight(previewHeight);
        setScaleFilter();
        setYtdlRawOptions();
        updateWidth(aspectRatio);
    }

    setPreviewPosition(where, mainWindowWidth);

    if (CacheEntry *hit = findCache(videoPosition)) {
        debounce.stop();
        showCachedImage(hit->image);
    } else {
        showLiveVideo();
        debounce.start();
    }
    show();
}

void VideoPreview::setPreviewPosition(const QPoint &where, int mainWindowWidth)
{
    int tooltipWidth = videoContainer->width();
    int xPos = where.x() - std::round(tooltipWidth / 2);
    if (xPos + tooltipWidth + previewMarginX > mainWindowWidth)
        xPos = mainWindowWidth - tooltipWidth - previewMarginX;
    else if (xPos < previewMarginX)
        xPos = previewMarginX;
    previewBottomLeft = QPoint(xPos, where.y());
}

void VideoPreview::updateWidth(double newAspect)
{
    if (newAspect == 0) {
        aspectRatioSet = false;
        return;
    }
    aspectRatio = newAspect;
    double dpr = devicePixelRatioF();
    int newWidth = floor(round(videoWidget->height() * dpr) * newAspect) / dpr;
    videoWidget->setFixedWidth(newWidth);
    imageLabel->setFixedSize(newWidth, videoWidget->height());
    textLabel->setFixedSize(newWidth, labelHeight);
    videoWidget->move(shadowMargin, shadowMargin);
    imageLabel->move(shadowMargin, shadowMargin);
    textLabel->move(shadowMargin, shadowMargin + videoWidget->height());
    videoContainer->setFixedSize(newWidth + shadowMargin * 2,
                                 videoWidget->height() + labelHeight + shadowMargin * 2);
    aspectRatioSet = true;
    if (shouldBeShown)
        show();
    else
        hide();
}

void VideoPreview::setYtdlRawOptions()
{
    if (!client || !videoWidget)
        return;
    client->setOption("ytdl-raw-options", QString("js-runtimes=quickjs,"\
                                        "remote-components=ejs:github,"\
                                        "format-sort=[res:%1,+size,+br,+fps]").arg(videoWidget->height()));
}

void VideoPreview::setScaleFilter()
{
    if (!client || lastPixelHeight <= 0)
        return;
    int evenHeight = qMax(2, (lastPixelHeight / 2) * 2);
    client->setOption("vf", QString("scale=-2:%1").arg(evenHeight));
}

void VideoPreview::requestThumbnail()
{
    if (CacheEntry *hit = findCache(pendingTime)) {
        showCachedImage(hit->image);
        return;
    }
    if (lastSeekTime >= 0 && timeBucket(pendingTime) == timeBucket(lastSeekTime))
        return;
    if (!client)
        return;
    lastSeekTime = pendingTime;
    grabPending = true;
    showLiveVideo();
    client->seek(pendingTime);
    videoWidget->update();
}

void VideoPreview::onFrameSwapped()
{
    if (!grabPending || !videoWidget)
        return;
    QImage image = videoWidget->grabFramebuffer();
    if (image.isNull() || image.width() < 2 || image.height() < 2)
        return;
    grabPending = false;
    storeCache(lastSeekTime, image);
}

void VideoPreview::showCachedImage(const QImage &image)
{
    if (image.isNull())
        return;
    double dpr = devicePixelRatioF();
    QPixmap pm = QPixmap::fromImage(image);
    pm.setDevicePixelRatio(dpr);
    imageLabel->setPixmap(pm);
    imageLabel->setFixedSize(videoWidget->width(), videoWidget->height());
    imageLabel->show();
    imageLabel->raise();
}

void VideoPreview::showLiveVideo()
{
    imageLabel->hide();
}

qint64 VideoPreview::timeBucket(double time)
{
    return qint64(std::llround(time / cacheBucketSec));
}

VideoPreview::CacheEntry *VideoPreview::findCache(double time)
{
    qint64 bucket = timeBucket(time);
    for (CacheEntry &entry : cache) {
        if (entry.bucket == bucket)
            return &entry;
    }
    return nullptr;
}

void VideoPreview::storeCache(double requestedTime, const QImage &image)
{
    qint64 bucket = timeBucket(requestedTime);
    for (int i = 0; i < cache.size(); i++) {
        if (cache[i].bucket == bucket) {
            cache[i].image = image;
            cache.move(i, 0);
            return;
        }
    }
    cache.prepend({bucket, image});
    while (cache.size() > cacheLimit)
        cache.removeLast();
}

void VideoPreview::show()
{
    if (!aspectRatioSet) {
        shouldBeShown = true;
        return;
    }
    shouldBeShown = true;
    videoContainer->move(previewBottomLeft.x(),
                         previewBottomLeft.y() - videoContainer->height());
}

void VideoPreview::hide() {
    shouldBeShown = false;
    debounce.stop();
    videoContainer->move(-50000, -50000);
}
