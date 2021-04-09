/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "platformqpaintersurfacetexture.h"

namespace KWin
{

class KWIN_EXPORT PlatformQPainterSurfaceTextureInternal : public PlatformQPainterSurfaceTexture
{
public:
    PlatformQPainterSurfaceTextureInternal(QPainterBackend *backend, SurfaceTextureInternal *texture);

    bool create() override;
    void update(const QRegion &region) override;

private:
    SurfaceTextureInternal *m_texture;
};

} // namespace KWin
