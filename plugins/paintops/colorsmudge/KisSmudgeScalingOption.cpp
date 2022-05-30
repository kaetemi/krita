/* This file is part of the KDE project
 *
 * SPDX-FileCopyrightText: 2022 Jan Boon <jan.boon@kaetemi.be>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KisSmudgeScalingOption.h"

KisSmudgeScalingOption::KisSmudgeScalingOption()
    : KisCurveOption(KoID("SmudgeScaling", i18n("Smudge Scaling")), KisPaintOpOption::GENERAL, false)
{
    setValueRange(1.0, 2.0);
}
