/*
    SPDX-FileCopyrightText: 2020 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "eglonxbackend.h"
#include "platformopenglsurfacetexture_x11.h"

#include <kwingltexture.h>
#include <kwingltexture_p.h>

namespace KWin
{

class EglTexturePrivate;
class SoftwareVsyncMonitor;
class X11StandalonePlatform;

class EglBackend : public EglOnXBackend
{
    Q_OBJECT

public:
    EglBackend(Display *display, X11StandalonePlatform *platform);
    ~EglBackend() override;

    PlatformSurfaceTexture *createPlatformSurfaceTextureX11(SurfaceTextureX11 *texture) override;
    QRegion beginFrame(int screenId) override;
    void endFrame(int screenId, const QRegion &damage, const QRegion &damagedRegion) override;
    void screenGeometryChanged(const QSize &size) override;

private:
    void presentSurface(EGLSurface surface, const QRegion &damage, const QRect &screenGeometry);
    void vblank(std::chrono::nanoseconds timestamp);

    X11StandalonePlatform *m_backend;
    SoftwareVsyncMonitor *m_vsyncMonitor;
    int m_bufferAge = 0;
};

class EglTexture : public GLTexture
{
public:
    explicit EglTexture(EglBackend *backend);

    bool create(SurfaceTextureX11 *texture);

private:
    Q_DECLARE_PRIVATE(EglTexture)
};

class EglTexturePrivate : public GLTexturePrivate
{
public:
    EglTexturePrivate(EglTexture *texture, EglBackend *backend);
    ~EglTexturePrivate() override;

    bool create(SurfaceTextureX11 *texture);

protected:
    void onDamage() override;

private:
    EglTexture *q;
    EglBackend *m_backend;
    EGLImageKHR m_image = EGL_NO_IMAGE_KHR;
};

class EglSurfaceTextureX11 : public PlatformOpenGLSurfaceTextureX11
{
public:
    EglSurfaceTextureX11(EglBackend *backend, SurfaceTextureX11 *texture);

    bool create() override;
    void update(const QRegion &region) override;
};

} // namespace KWin
