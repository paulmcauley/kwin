/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "platformqpaintersurfacetexture.h"

namespace KWin
{

class KWIN_EXPORT PlatformQPainterSurfaceTextureWayland : public PlatformQPainterSurfaceTexture
{
public:
    PlatformQPainterSurfaceTextureWayland(QPainterBackend *backend, SurfaceTextureWayland *pixmap);

    bool create() override;
    void update(const QRegion &region) override;

private:
    SurfaceTextureWayland *m_texture;
};

} // namespace KWin
