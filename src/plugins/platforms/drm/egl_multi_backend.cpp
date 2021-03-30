/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2020 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "egl_multi_backend.h"
#include "logging.h"
#include "egl_gbm_backend.h"

namespace KWin
{

EglMultiBackend::EglMultiBackend(DrmBackend *backend, AbstractEglDrmBackend *backend0)
    : OpenGLBackend()
    , m_backend(backend)
{
    m_eglBackends.append(backend0);
    setIsDirectRendering(true);
}

EglMultiBackend::~EglMultiBackend()
{
    for (int i = 1; i < m_eglBackends.count(); i++) {
        delete m_eglBackends[i];
    }
    // delete primary backend last, or this will crash!
    delete m_eglBackends[0];
}

void EglMultiBackend::init()
{
    for (auto b : qAsConst(m_eglBackends)) {
        b->init();
    }
    // if any don't support it set it to not supported
    setSupportsBufferAge(true);
    setSupportsPartialUpdate(true);
    setSupportsSwapBuffersWithDamage(true);
    for (auto b : qAsConst(m_eglBackends)) {
        if (!b->supportsBufferAge()) {
            setSupportsBufferAge(false);
        }
        if (!b->supportsPartialUpdate()) {
            setSupportsPartialUpdate(false);
        }
        if (!b->supportsSwapBuffersWithDamage()) {
            setSupportsSwapBuffersWithDamage(false);
        }
    }
    // we only care about the rendering GPU here
    setSupportsSurfacelessContext(m_eglBackends[0]->supportsSurfacelessContext());
    // these are client extensions and the same for all egl backends
    setExtensions(m_eglBackends[0]->extensions());

    m_eglBackends[0]->makeCurrent();
}

QRegion EglMultiBackend::beginFrame(int screenId)
{
    int internalScreenId;
    AbstractEglBackend *backend = findBackend(screenId, internalScreenId);
    Q_ASSERT(backend != nullptr);
    return backend->beginFrame(internalScreenId);
}

void EglMultiBackend::endFrame(int screenId, const QRegion &damage, const QRegion &damagedRegion)
{
    int internalScreenId;
    AbstractEglBackend *backend = findBackend(screenId, internalScreenId);
    Q_ASSERT(backend != nullptr);
    backend->endFrame(internalScreenId, damage, damagedRegion);
}

bool EglMultiBackend::scanout(int screenId, SurfaceItem *surfaceItem)
{
    int internalScreenId;
    AbstractEglBackend *backend = findBackend(screenId, internalScreenId);
    Q_ASSERT(backend != nullptr);
    return backend->scanout(internalScreenId, surfaceItem);
}

bool EglMultiBackend::makeCurrent()
{
    return m_eglBackends[0]->makeCurrent();
}

void EglMultiBackend::doneCurrent()
{
    m_eglBackends[0]->doneCurrent();
}

SceneOpenGLTexturePrivate *EglMultiBackend::createBackendTexture(SceneOpenGLTexture *texture)
{
    return m_eglBackends[0]->createBackendTexture(texture);
}

QSharedPointer<GLTexture> EglMultiBackend::textureForOutput(AbstractOutput *requestedOutput) const
{
    // this assumes that the wrong backends return {}
    for (auto backend : qAsConst(m_eglBackends)) {
        auto texture = backend->textureForOutput(requestedOutput);
        if (!texture.isNull()) {
            return texture;
        }
    }
    return {};
}

void EglMultiBackend::screenGeometryChanged(const QSize &size)
{
    Q_UNUSED(size)
}

AbstractEglDrmBackend *EglMultiBackend::findBackend(int screenId, int& internalScreenId) const
{
    int screens = 0;
    for (int i = 0; i < m_eglBackends.count(); i++) {
        if (screenId < screens + m_eglBackends[i]->screenCount()) {
            internalScreenId = screenId - screens;
            return m_eglBackends[i];
        }
        screens += m_eglBackends[i]->screenCount();
    }
    qCDebug(KWIN_DRM) << "could not find backend!" << screenId << "/" << screens;
    return nullptr;
}

void EglMultiBackend::addGpu(DrmGpu *gpu)
{
    // secondary GPUs are atm guaranteed to use gbm
    m_eglBackends.append(new EglGbmBackend(m_backend, gpu));
}

void EglMultiBackend::removeGpu(DrmGpu *gpu)
{
    for (const auto &backend : m_eglBackends) {
        if (backend->gpu() == gpu) {
            m_eglBackends.removeOne(backend);
            delete backend;
            return;
        }
    }
}

void EglMultiBackend::addBackend(AbstractEglDrmBackend *backend)
{
    m_eglBackends.append(backend);
}

bool EglMultiBackend::directScanoutAllowed(int screenId) const
{
    int internalScreenId;
    AbstractEglBackend *backend = findBackend(screenId, internalScreenId);
    Q_ASSERT(backend != nullptr);
    return backend->directScanoutAllowed(internalScreenId);
}

}
