/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2020 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "drm_gpu.h"

#include "drm_backend.h"
#include "drm_output.h"
#include "drm_object_connector.h"
#include "drm_object_crtc.h"
#include "abstract_egl_backend.h"
#include "logging.h"
#include "session.h"
#include "renderloop_p.h"
#include "drm_pipeline.h"
#if HAVE_GBM
#include "egl_gbm_backend.h"
#include <gbm.h>
#include "gbm_dmabuf.h"
#endif
// system
#include <algorithm>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
// drm
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/drm_mode.h>

namespace KWin
{

DrmGpu::DrmGpu(DrmBackend *backend, QByteArray devNode, int fd, int drmId)
    : m_backend(backend)
    , m_devNode(devNode)
    , m_fd(fd)
    , m_drmId(drmId)
    , m_atomicModeSetting(false)
    , m_gbmDevice(nullptr)
{
    uint64_t capability = 0;

    if (drmGetCap(fd, DRM_CAP_CURSOR_WIDTH, &capability) == 0) {
        m_cursorSize.setWidth(capability);
    } else {
        m_cursorSize.setWidth(64);
    }

    if (drmGetCap(fd, DRM_CAP_CURSOR_HEIGHT, &capability) == 0) {
        m_cursorSize.setHeight(capability);
    } else {
        m_cursorSize.setHeight(64);
    }

    int ret = drmGetCap(fd, DRM_CAP_TIMESTAMP_MONOTONIC, &capability);
    if (ret == 0 && capability == 1) {
        m_presentationClock = CLOCK_MONOTONIC;
    } else {
        m_presentationClock = CLOCK_REALTIME;
    }

    if (!qEnvironmentVariableIsSet("KWIN_DRM_NO_MODIFIERS")) {
        m_addFB2ModifiersSupported = drmGetCap(fd, DRM_CAP_ADDFB2_MODIFIERS, &capability) && capability == 1;
        qCDebug(KWIN_DRM) << "drmModeAddFB2WithModifiers is" << (m_addFB2ModifiersSupported ? "supported" : "not supported");
    }

    // find out if this GPU is using the NVidia proprietary driver
    DrmScopedPointer<drmVersion> version(drmGetVersion(fd));
    m_useEglStreams = strstr(version->name, "nvidia-drm");

    m_socketNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_socketNotifier, &QSocketNotifier::activated, this, &DrmGpu::dispatchEvents);
}

DrmGpu::~DrmGpu()
{
    waitIdle();
    if (m_eglDisplay != EGL_NO_DISPLAY) {
        eglTerminate(m_eglDisplay);
    }
#if HAVE_GBM
    if (m_gbmDevice) {
        gbm_device_destroy(m_gbmDevice);
    }
#endif
    qDeleteAll(m_outputs);
    qDeleteAll(m_pipelines);
    qDeleteAll(m_planes);
    delete m_socketNotifier;
    m_backend->session()->closeRestricted(m_fd);
}

clockid_t DrmGpu::presentationClock() const
{
    return m_presentationClock;
}

void DrmGpu::tryAMS()
{
    m_atomicModeSetting = false;
    if (drmSetClientCap(m_fd, DRM_CLIENT_CAP_ATOMIC, 1) == 0) {
        m_atomicModeSetting = true;
        DrmScopedPointer<drmModePlaneRes> planeResources(drmModeGetPlaneResources(m_fd));
        if (!planeResources) {
            qCWarning(KWIN_DRM) << "Failed to get plane resources. Falling back to legacy mode on GPU " << m_devNode;
            m_atomicModeSetting = false;
            return;
        }
        qCDebug(KWIN_DRM) << "Using Atomic Mode Setting on gpu" << m_devNode;
        qCDebug(KWIN_DRM) << "Number of planes on GPU" << m_devNode << ":" << planeResources->count_planes;
        // create the plane objects
        for (unsigned int i = 0; i < planeResources->count_planes; ++i) {
            DrmScopedPointer<drmModePlane> kplane(drmModeGetPlane(m_fd, planeResources->planes[i]));
            DrmPlane *p = new DrmPlane(this, kplane->plane_id);
            if (p->init()) {
                m_planes << p;
            } else {
                delete p;
            }
        }
        if (m_planes.isEmpty()) {
            qCWarning(KWIN_DRM) << "Failed to create any plane. Falling back to legacy mode on GPU " << m_devNode;
            m_atomicModeSetting = false;
        }
        m_unusedPlanes = m_planes;
    } else {
        qCWarning(KWIN_DRM) << "drmSetClientCap for Atomic Mode Setting failed. Using legacy mode on GPU" << m_devNode;
    }
}

bool DrmGpu::updateOutputs()
{
    DrmScopedPointer<drmModeRes> resources(drmModeGetResources(m_fd));
    if (!resources) {
        qCWarning(KWIN_DRM) << "drmModeGetResources failed";
        return false;
    }

    for (const auto &output : qAsConst(m_outputs)) {
        auto pipeline = output->m_pipeline;
        if (!pipeline->connector()->isConnected()) {
            qCDebug(KWIN_DRM) << "removing output" << output;

            // remove output
            m_outputs.removeOne(output);
            Q_ASSERT(!m_outputs.contains(output));
            m_pipelines.removeOne(pipeline);
            Q_ASSERT(!m_pipelines.contains(pipeline));
            output->teardown();
            emit outputRemoved(output);

            // clean up pipeline and resources
            if (pipeline->primaryPlane()) {
                m_unusedPlanes << pipeline->primaryPlane();
            }
            if (pipeline->cursorPlane()) {
                m_unusedPlanes << pipeline->cursorPlane();
            }
            m_unusedPlanes << pipeline->overlayPlanes();
            auto connector = pipeline->connector();
            auto crtc = pipeline->crtc();
            delete pipeline;
            delete connector;
            delete crtc;
            output->m_pipeline = nullptr;
        }
    }

    QVector<DrmConnector*> connectors;
    QVector<DrmCrtc*> crtcs;

    for (int i = 0; i < resources->count_connectors; ++i) {
        const uint32_t currentConnector = resources->connectors[i];
        auto it = std::find_if(m_pipelines.constBegin(), m_pipelines.constEnd(), [currentConnector] (DrmPipeline *p) { return p->connector()->id() == currentConnector; });
        if (it == m_pipelines.constEnd()) {
            auto c = new DrmConnector(this, currentConnector);
            if (!c->init()) {
                delete c;
                continue;
            }
            if (c->isNonDesktop()) {
                delete c;
                continue;
            }
            connectors << c;
        }
    }

    for (int i = 0; i < resources->count_crtcs; ++i) {
        const uint32_t currentCrtc = resources->crtcs[i];
        auto it = std::find_if(m_pipelines.constBegin(), m_pipelines.constEnd(), [currentCrtc] (DrmPipeline *p) { return p->crtc()->id() == currentCrtc; });
        if (it == m_pipelines.constEnd()) {
            auto c = new DrmCrtc(this, currentCrtc, i);
            if (!c->init()) {
                delete c;
                continue;
            }
            crtcs << c;
        }
    }

    auto pipelines = findWorkingCombination(connectors, crtcs, m_unusedPlanes);
    if (pipelines.count() < connectors.count()) {
        qDeleteAll(pipelines);
        pipelines = shufflePipelines(connectors, crtcs);
    }
    for (const auto &pipeline : qAsConst(pipelines)) {
        DrmOutput *output = new DrmOutput(this->m_backend, this, pipeline);
        qCDebug(KWIN_DRM) << "Found new output" << output;
        if (!output->initCursor(m_cursorSize)) {
            m_backend->setSoftwareCursorForced(true);
        }
        connectors.removeOne(pipeline->connector());
        crtcs.removeOne(pipeline->crtc());
        m_unusedPlanes.removeOne(pipeline->primaryPlane());
        m_unusedPlanes.removeOne(pipeline->cursorPlane());
        m_outputs << output;
        m_pipelines << pipeline;
        emit outputAdded(output);
    }
    qDeleteAll(connectors);
    qDeleteAll(crtcs);
    return true;
}

QVector<DrmPipeline*> DrmGpu::shufflePipelines(QVector<DrmConnector*> &unusedConnectors, QVector<DrmCrtc*> &unusedCrtcs)
{
    qCWarning(KWIN_DRM) << "Turning outputs off to find working configuation";
    QVector<DrmConnector*> connectors = unusedConnectors;
    QVector<DrmCrtc*> crtcs = unusedCrtcs;
    for (const auto &output : qAsConst(m_outputs)) {
        if (output->m_pipeline->setEnablement(false)) {
            connectors << output->m_pipeline->connector();
            crtcs << output->m_pipeline->crtc();
        } else {
            qCWarning(KWIN_DRM) << "Disabling pipeline failed!";
        }
    }

    auto workingPipelines = findWorkingCombination(connectors, crtcs, m_planes);
    if (workingPipelines.count() == unusedConnectors.count()) {
        // replace the old pipelines with the new ones
        for (const auto &output : qAsConst(m_outputs)) {
            for (const auto &pipeline : qAsConst(workingPipelines)) {
                if (pipeline->connector() == output->connector()) {
                    delete output->m_pipeline;
                    output->setPipeline(pipeline);
                    workingPipelines.removeOne(pipeline);
                    unusedConnectors.removeOne(pipeline->connector());
                    unusedCrtcs.removeOne(pipeline->crtc());
                    break;
                }
            }
        }
    } else {
        qCWarning(KWIN_DRM) << "Could not find working combination of connectors and crtcs! Reverting to old configuration.";
        for (const auto &output : qAsConst(m_outputs)) {
            if (!output->m_pipeline->setEnablement(output->m_dpmsEnabled)) {
                qCWarning(KWIN_DRM) << "Setting enablement failed!";
            }
        }
        return findWorkingCombination(unusedConnectors, unusedCrtcs, m_unusedPlanes);
    }
    return workingPipelines;
}

QVector<DrmPipeline*> DrmGpu::findWorkingCombination(QVector<DrmConnector*> connectors, QVector<DrmCrtc*> crtcs, QVector<DrmPlane*> planes)
{
    if (!connectors.count()) {
        return {};
    }
    QVector<DrmPipeline*> mostWorkingPipelines;
    DrmConnector *connector = connectors[0];
    connectors.removeFirst();

    QVector<uint32_t> encoders = connector->encoders();
    for (auto encId : qAsConst(encoders)) {
        DrmScopedPointer<drmModeEncoder> encoder(drmModeGetEncoder(m_fd, encId));
        if (!encoder) {
            continue;
        }
        for (DrmCrtc *crtc : qAsConst(crtcs)) {
            if (!(encoder->possible_crtcs & (1 << crtc->pipeIndex()))) {
                continue;
            }
            if (m_atomicModeSetting) {
                for (const auto &plane : planes) {
                    if (plane->isCrtcSupported(crtc->pipeIndex()) &&
                            plane->type() == DrmPlane::TypeIndex::Primary) {
                        DrmPipeline *pipeline = new DrmPipeline(this, connector, crtc, plane, nullptr);
                        if (!pipeline->test()) {
                            qCDebug(KWIN_DRM, "Test failed for %d, %d", crtc->id(), plane->id());
                            delete pipeline;
                            continue;
                        }
                        auto remainingCrtcs = crtcs;
                        remainingCrtcs.removeOne(crtc);
                        auto remainingPlanes = planes;
                        remainingPlanes.removeOne(plane);

                        auto pipelines = findWorkingCombination(connectors, remainingCrtcs, remainingPlanes);
                        pipelines.prepend(pipeline);
                        if (pipelines.count() > connectors.count()) {
                            return pipelines;
                        } else if (pipelines.count() > mostWorkingPipelines.count()) {
                            qDeleteAll(mostWorkingPipelines);
                            mostWorkingPipelines = pipelines;
                        } else {
                            qDeleteAll(pipelines);
                        }
                    }
                }
            } else {
                // no planes and no test
                DrmPipeline *pipeline = new DrmPipeline(this, connector, crtc, nullptr, nullptr);
                auto remainingConnectors = connectors;
                remainingConnectors.removeOne(connector);
                auto remainingCrtcs = crtcs;
                remainingCrtcs.removeOne(crtc);
                auto pipelines = findWorkingCombination(remainingConnectors, remainingCrtcs, planes);
                pipelines << pipeline;
                return pipelines;
            }
        }
    }
    return mostWorkingPipelines;
}

DrmOutput *DrmGpu::findOutput(quint32 connector)
{
    auto it = std::find_if(m_outputs.constBegin(), m_outputs.constEnd(), [connector] (DrmOutput *o) {
        return o->m_conn->id() == connector;
    });
    if (it != m_outputs.constEnd()) {
        return *it;
    }
    return nullptr;
}

DrmPlane *DrmGpu::getCompatiblePlane(QVector<DrmPlane*> planes, DrmPlane::TypeIndex typeIndex, DrmCrtc *crtc)
{
    for (auto plane : planes) {
        if (plane->type() != typeIndex) {
            continue;
        }
        if (plane->isCrtcSupported(crtc->pipeIndex())) {
            return plane;
        }
    }
    return nullptr;
}

void DrmGpu::waitIdle()
{
    m_socketNotifier->setEnabled(false);
    while (true) {
        const bool idle = std::all_of(m_outputs.constBegin(), m_outputs.constEnd(), [](DrmOutput *output){
            return !output->m_pageFlipPending;
        });
        if (idle) {
            break;
        }
        pollfd pfds[1];
        pfds[0].fd = m_fd;
        pfds[0].events = POLLIN;

        const int ready = poll(pfds, 1, 30000);
        if (ready < 0) {
            if (errno != EINTR) {
                qCWarning(KWIN_DRM) << Q_FUNC_INFO << "poll() failed:" << strerror(errno);
                break;
            }
        } else if (ready == 0) {
            qCWarning(KWIN_DRM) << "No drm events for gpu" << m_devNode << "within last 30 seconds";
            break;
        } else {
            dispatchEvents();
        }
    };
    m_socketNotifier->setEnabled(true);
}

static std::chrono::nanoseconds convertTimestamp(const timespec &timestamp)
{
    return std::chrono::seconds(timestamp.tv_sec) + std::chrono::nanoseconds(timestamp.tv_nsec);
}

static std::chrono::nanoseconds convertTimestamp(clockid_t sourceClock, clockid_t targetClock,
                                                 const timespec &timestamp)
{
    if (sourceClock == targetClock) {
        return convertTimestamp(timestamp);
    }

    timespec sourceCurrentTime = {};
    timespec targetCurrentTime = {};

    clock_gettime(sourceClock, &sourceCurrentTime);
    clock_gettime(targetClock, &targetCurrentTime);

    const auto delta = convertTimestamp(sourceCurrentTime) - convertTimestamp(timestamp);
    return convertTimestamp(targetCurrentTime) - delta;
}

static void pageFlipHandler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data)
{
    Q_UNUSED(fd)
    Q_UNUSED(frame)

    auto output = static_cast<DrmOutput *>(data);
    if (!output) {
        return;
    }

    std::chrono::nanoseconds timestamp = convertTimestamp(output->gpu()->presentationClock(),
                                                          CLOCK_MONOTONIC,
                                                          { sec, usec * 1000 });
    if (timestamp == std::chrono::nanoseconds::zero()) {
        qCDebug(KWIN_DRM, "Got invalid timestamp (sec: %u, usec: %u) on output %s",
                sec, usec, qPrintable(output->name()));
        timestamp = std::chrono::steady_clock::now().time_since_epoch();
    }

    output->pageFlipped();
    RenderLoopPrivate *renderLoopPrivate = RenderLoopPrivate::get(output->renderLoop());
    renderLoopPrivate->notifyFrameCompleted(timestamp);
}

void DrmGpu::dispatchEvents()
{
    if (!m_backend->session()->isActive()) {
        return;
    }
    drmEventContext context = {};
    context.version = 2;
    context.page_flip_handler = pageFlipHandler;
    drmHandleEvent(m_fd, &context);
}

}
