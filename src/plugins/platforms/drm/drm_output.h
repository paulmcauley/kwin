/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_DRM_OUTPUT_H
#define KWIN_DRM_OUTPUT_H

#include "abstract_wayland_output.h"
#include "drm_pointer.h"
#include "drm_object.h"
#include "drm_object_plane.h"

#include <QObject>
#include <QPoint>
#include <QSize>
#include <QVector>
#include <QSharedPointer>
#include <xf86drmMode.h>

namespace KWin
{

class DrmBackend;
class DrmBuffer;
class DrmDumbBuffer;
class DrmPlane;
class DrmConnector;
class DrmCrtc;
class Cursor;
class DrmGpu;

class KWIN_EXPORT DrmOutput : public AbstractWaylandOutput
{
    Q_OBJECT
public:
    ///deletes the output, calling this whilst a page flip is pending will result in an error
    ~DrmOutput() override;

    RenderLoop *renderLoop() const override;

    ///queues deleting the output after a page flip has completed.
    void teardown();
    void releaseBuffers();
    bool showCursor(DrmDumbBuffer *buffer);
    bool showCursor();
    bool hideCursor();
    bool updateCursor();
    void moveCursor();
    bool init(drmModeConnector *connector);
    bool present(const QSharedPointer<DrmBuffer> &buffer);
    void pageFlipped();

    bool isDpmsEnabled() const {
        // We care for current as well as pending mode in order to allow first present in AMS.
        return m_dpmsModePending == DpmsMode::On;
    }

    DpmsMode dpmsModePending() const {
        return m_dpmsModePending;
    }

    const DrmCrtc *crtc() const {
        return m_crtc;
    }
    const DrmConnector *connector() const {
        return m_conn;
    }
    const DrmPlane *primaryPlane() const {
        return m_primaryPlane;
    }

    bool initCursor(const QSize &cursorSize);

    /**
     * Drm planes might be capable of realizing the current output transform without usage
     * of compositing. This is a getter to query the current state of that
     *
     * @return true if the hardware realizes the transform without further assistance
     */
    bool hardwareTransforms() const;

    DrmGpu *gpu() {
        return m_gpu;
    }

private:
    friend class DrmGpu;
    friend class DrmBackend;
    friend class DrmCrtc;   // TODO: For use of setModeLegacy. Remove later when we allow multiple connectors per crtc
                            //       and save the connector ids in the DrmCrtc instance.
    DrmOutput(DrmBackend *backend, DrmGpu* gpu);

    bool presentAtomically(const QSharedPointer<DrmBuffer> &buffer);

    enum class AtomicCommitMode {
        Test,
        Real
    };
    bool doAtomicCommit(AtomicCommitMode mode);

    bool presentLegacy(const QSharedPointer<DrmBuffer> &buffer);
    bool setModeLegacy(DrmBuffer *buffer);
    void initOutputDevice(drmModeConnector *connector);

    bool isCurrentMode(const drmModeModeInfo *mode) const;

    void atomicEnable();
    void atomicDisable();
    void updateEnablement(bool enable) override;

    bool dpmsAtomicOff();
    bool dpmsLegacyApply();

    void dpmsFinishOn();
    void dpmsFinishOff();

    bool atomicReqModesetPopulate(drmModeAtomicReq *req, bool enable);
    void setDpmsMode(DpmsMode mode) override;
    void updateMode(int modeIndex) override;
    void updateMode(uint32_t width, uint32_t height, uint32_t refreshRate);
    void setCurrentModeInternal();

    void updateTransform(Transform transform) override;

    int gammaRampSize() const override;
    bool setGammaRamp(const GammaRamp &gamma) override;
    void setOverscan(uint32_t overscan) override;

    DrmBackend *m_backend;
    DrmGpu *m_gpu;
    DrmConnector *m_conn = nullptr;
    DrmCrtc *m_crtc = nullptr;
    bool m_lastGbm = false;
    drmModeModeInfo m_mode;
    DpmsMode m_dpmsModePending = DpmsMode::On;
    RenderLoop *m_renderLoop;

    uint32_t m_blobId = 0;
    DrmPlane *m_primaryPlane = nullptr;
    DrmPlane *m_cursorPlane = nullptr;
    QVector<DrmPlane*> m_nextPlanesFlipList;
    bool m_pageFlipPending = false;
    bool m_atomicOffPending = false;
    bool m_modesetRequested = true;

    struct {
        Transform transform;
        drmModeModeInfo mode;
        DrmPlane::Transformations planeTransformations;
        QPoint globalPos;
        bool valid = false;
    } m_lastWorkingState;
    QScopedPointer<DrmDumbBuffer> m_cursor[2];
    int m_cursorIndex = 0;
    bool m_hasNewCursor = false;
    bool m_deleted = false;
};

}

Q_DECLARE_METATYPE(KWin::DrmOutput*)

#endif
