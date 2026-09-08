#ifndef VIDEOPREVIEW_H
#define VIDEOPREVIEW_H

#include <QImage>
#include <QLabel>
#include <QList>
#include <QTimer>
#include <QUrl>
#include "thumbnailclient.h"

class VideoPreview : public QWidget {
    public:
        explicit VideoPreview(QWidget *parent = nullptr);
        ~VideoPreview();
        void openFile(const QUrl &fileUrl);
        void updatePalette();
        void show(const QString &text, double videoPosition, const QPoint &where, int mainWindowWidth, int previewHeight);
        void hide();

    private:
        struct CacheEntry {
            qint64 bucket = 0;
            QImage image;
        };

        static qint64 timeBucket(double time);
        void setPreviewPosition(const QPoint &where, int mainWindowWidth);
        void show();
        void updateWidth(double newAspect);
        void setYtdlRawOptions();
        void setScaleFilter();
        void requestThumbnail();
        void onFrameSwapped();
        CacheEntry *findCache(double time);
        void storeCache(double requestedTime, const QImage &image);
        void showCachedImage(const QImage &image);
        void showLiveVideo();

        QLabel *textLabel = nullptr;
        QLabel *imageLabel = nullptr;
        ThumbnailClient *client = nullptr;
        ThumbnailGlWidget *videoWidget = nullptr;
        QWidget *videoContainer = nullptr;
        QTimer debounce;
        QList<CacheEntry> cache;
        double aspectRatio = 0;
        bool aspectRatioSet = false;
        bool shouldBeShown = false;
        bool grabPending = false;
        QPoint previewBottomLeft;
        int lastPixelHeight = 0;
        double pendingTime = 0;
        double lastSeekTime = -1;
    };

#endif // VIDEOPREVIEW_H
