/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2020 Henri Chain <henri.chain@enioka.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "rulebooksettings.h"
#include "rulesettings.h"

#include <QUuid>

namespace KWin
{
RuleBookSettings::RuleBookSettings(KSharedConfig::Ptr config, QObject *parent)
    : RuleBookSettingsBase(config, parent)
{
    usrRead();
}

RuleBookSettings::RuleBookSettings(const QString &configname, KConfig::OpenFlags flags, QObject *parent)
    : RuleBookSettings(KSharedConfig::openConfig(configname, flags), parent)
{
}

RuleBookSettings::RuleBookSettings(KConfig::OpenFlags flags, QObject *parent)
    : RuleBookSettings(QStringLiteral("kwinrulesrc"), flags, parent)
{
}

RuleBookSettings::RuleBookSettings(QObject *parent)
    : RuleBookSettings(KConfig::FullConfig, parent)
{
}

void RuleBookSettings::setRules(const QVector<Rules *> &rules)
{
    mRuleGroupList.clear();

    int i = 0;
    const int list_length = m_list.length();
    for (const auto &rule : rules) {
        RuleSettings *settings;
        if (i < list_length) {
            // Optimization. Reuse RuleSettings already created
            settings = m_list.at(i);
            settings->setDefaults();
        } else {
            // If there are more rules than in cache
            settings = new RuleSettings(this->sharedConfig(), generateGroupName(), this);
            m_list.append(settings);
        }

        rule->write(settings);
        mRuleGroupList.append(settings->currentGroup());

        i++;
    }

    for (int j = m_list.count() - 1; j < rules.count(); j++) {
        delete m_list[j];
        m_list.removeAt(j);
    }

    mCount = mRuleGroupList.count();
}

QVector<Rules *> RuleBookSettings::rules()
{
    QVector<Rules *> result;
    result.reserve(m_list.count());
    for (const auto &settings : qAsConst(m_list)) {
        result.append(new Rules(settings));
    }
    return result;
}

bool RuleBookSettings::usrSave()
{
    bool result = true;
    for (const auto &settings : qAsConst(m_list)) {
        result &= settings->save();
    }

    // Remove deleted groups from config
    for (const QString &groupName : m_lastLoadedGroups) {
        if (sharedConfig()->hasGroup(groupName) && !ruleGroupList().contains(groupName)) {
            sharedConfig()->deleteGroup(groupName);
        }
    }

    return result;
}

void RuleBookSettings::usrRead()
{
    qDeleteAll(m_list);
    m_list.clear();

    // Legacy path for backwards compatibility with older config files without a rules list
    if (ruleGroupList().isEmpty() && count() > 0) {
        for (int i = 1; i <= count(); i++) {
            mRuleGroupList.append(QString::number(i));
        }
    }

    for (const QString &groupName : ruleGroupList()) {
        m_list.append(new RuleSettings(sharedConfig(), groupName, this));
    }

    mCount = m_list.count();

    m_lastLoadedGroups = ruleGroupList();
}

QString RuleBookSettings::generateGroupName()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
}
