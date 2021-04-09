/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "surfaceitem.h"

namespace KWin
{

SurfaceItem::SurfaceItem(Scene::Window *window, Item *parent)
    : Item(window, parent)
{
}

QPointF SurfaceItem::mapToWindow(const QPointF &point) const
{
    return rootPosition() + point - window()->pos();
}

QRegion SurfaceItem::shape() const
{
    return QRegion();
}

QRegion SurfaceItem::opaque() const
{
    return QRegion();
}

void SurfaceItem::addDamage(const QRegion &region)
{
    m_damage += region;
    scheduleRepaint(region);

    Toplevel *toplevel = window()->window();
    emit toplevel->damaged(toplevel, region);
}

void SurfaceItem::resetDamage()
{
    m_damage = QRegion();
}

QRegion SurfaceItem::damage() const
{
    return m_damage;
}

SurfaceTexture *SurfaceItem::texture() const
{
    if (m_texture && m_texture->isValid()) {
        return m_texture.data();
    }
    if (m_previousTexture && m_previousTexture->isValid()) {
        return m_previousTexture.data();
    }
    return nullptr;
}

SurfaceTexture *SurfaceItem::previousTexture() const
{
    return m_previousTexture.data();
}

void SurfaceItem::referencePreviousTexture()
{
    if (m_previousTexture && m_previousTexture->isDiscarded()) {
        m_referenceTextureCounter++;
    }
}

void SurfaceItem::unreferencePreviousTexture()
{
    if (!m_previousTexture || !m_previousTexture->isDiscarded()) {
        return;
    }
    m_referenceTextureCounter--;
    if (m_referenceTextureCounter == 0) {
        m_previousTexture.reset();
    }
}

void SurfaceItem::updateTexture()
{
    if (m_texture.isNull()) {
        m_texture.reset(createTexture());
    }
    if (m_texture->isValid()) {
        m_texture->update();
    } else {
        m_texture->create();
        if (m_texture->isValid()) {
            m_previousTexture.reset();
            discardQuads();
        }
    }
}

void SurfaceItem::discardTexture()
{
    if (!m_texture.isNull()) {
        if (m_texture->isValid()) {
            m_previousTexture.reset(m_texture.take());
            m_previousTexture->markAsDiscarded();
            m_referenceTextureCounter++;
        } else {
            m_texture.reset();
        }
    }
    addDamage(rect());
}

void SurfaceItem::preprocess()
{
    updateTexture();
}

PlatformSurfaceTexture::~PlatformSurfaceTexture()
{
}

SurfaceTexture::SurfaceTexture(PlatformSurfaceTexture *platformTexture, QObject *parent)
    : QObject(parent)
    , m_platformTexture(platformTexture)
{
}

void SurfaceTexture::update()
{
}

PlatformSurfaceTexture *SurfaceTexture::platformTexture() const
{
    return m_platformTexture.data();
}

bool SurfaceTexture::hasAlphaChannel() const
{
    return m_hasAlphaChannel;
}

QSize SurfaceTexture::size() const
{
    return m_size;
}

QRect SurfaceTexture::contentsRect() const
{
    return m_contentsRect;
}

bool SurfaceTexture::isDiscarded() const
{
    return m_isDiscarded;
}

void SurfaceTexture::markAsDiscarded()
{
    m_isDiscarded = true;
}

} // namespace KWin
