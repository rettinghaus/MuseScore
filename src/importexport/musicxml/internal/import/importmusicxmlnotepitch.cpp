/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "importmusicxmlnotepitch.h"

#include "draw/types/color.h"

#include "engraving/dom/factory.h"
#include "engraving/dom/score.h"

#include "../shared/musicxmlsupport.h"
#include "importmusicxmllogger.h"

#include "log.h"

using namespace mu::engraving;

namespace mu::iex::musicxml {
//---------------------------------------------------------
//   accidental
//---------------------------------------------------------

/**
 Parse the /score-partwise/part/measure/note/accidental node.
 Return the result as an Accidental.
 */

// TODO: split in reading parameters versus creation

static Accidental* accidental(muse::XmlStreamReader& e, Score* score)
{
    const bool cautionary = e.asciiAttribute("cautionary") == "yes";
    const bool editorial = e.asciiAttribute("editorial") == "yes";
    const bool parentheses = e.asciiAttribute("parentheses") == "yes";
    const bool noParentheses = e.asciiAttribute("parentheses") == "no";
    const bool brackets = e.asciiAttribute("bracket") == "yes";
    const bool noBrackets = e.asciiAttribute("bracket") == "no";
    const bool smallAccid = e.asciiAttribute("size") == "cue" || e.asciiAttribute("size") == "grace-cue";
    const Color accColor = Color(e.asciiAttribute("color").ascii());
    const String smufl = e.attribute("smufl");

    const String s = e.readText();
    const AccidentalType type = musicXmlString2accidentalType(s, smufl);

    if (type != AccidentalType::NONE) {
        Accidental* a = Factory::createAccidental(score->dummy());
        a->setAccidentalType(type);
        if (cautionary || editorial) { // no way to tell one from the other
            a->setRole(AccidentalRole::USER);
        } // except via the use of parentheses vs. brackets
        if (noParentheses || noBrackets) { // explicitly none wanted
        } else if (parentheses || cautionary) { // set to "yes" or "cautionary" and not set at all
            a->setBracket(AccidentalBracket(AccidentalBracket::PARENTHESIS));
        } else if (brackets || editorial) { // set to "yes" or "editorial" and not set at all
            a->setBracket(AccidentalBracket(AccidentalBracket::BRACKET));
        }
        if (accColor.isValid()) {
            a->setColor(accColor);
        }
        a->setSmall(smallAccid);
        return a;
    }

    return nullptr;
}

//---------------------------------------------------------
//   displayStepOctave
//---------------------------------------------------------

/**
 Handle <display-step> and <display-octave> for <rest> and <unpitched>
 */

void MusicXmlNotePitch::displayStepOctave(muse::XmlStreamReader& e)
{
    while (e.readNextStartElement()) {
        if (e.name() == "display-step") {
            const String step = e.readText();
            int pos = static_cast<int>(String(u"CDEFGAB").indexOf(step));
            if (step.size() == 1 && pos >= 0 && pos < 7) {
                m_displayStep = pos;
            } else {
                //logError(String("invalid step '%1'").arg(strStep));
                LOGD("invalid step '%s'", muPrintable(step));                // TODO
            }
        } else if (e.name() == "display-octave") {
            const String oct = e.readText();
            bool ok;
            m_displayOctave = oct.toInt(&ok);
            if (!ok || m_displayOctave < 0 || m_displayOctave > 9) {
                //logError(String("invalid octave '%1'").arg(strOct));
                LOGD("invalid octave '%s'", muPrintable(oct));               // TODO
                m_displayOctave = -1;
            }
        } else {
            e.skipCurrentElement();                         // TODO log
        }
    }
}

//---------------------------------------------------------
//   pitch
//---------------------------------------------------------

/**
 Parse the /score-partwise/part/measure/note/pitch node.
 */

void MusicXmlNotePitch::pitch(muse::XmlStreamReader& e)
{
    // defaults
    m_step = -1;
    m_alter = 0;
    m_tuning = 0.0;
    m_octave = -1;

    while (e.readNextStartElement()) {
        if (e.name() == "alter") {
            const String alter = e.readText();
            bool ok;
            m_alter = MusicXmlSupport::stringToInt(alter, &ok);             // fractions not supported by mscore
            if (!ok || m_alter < -2 || m_alter > 2) {
                m_logger->logError(String(u"invalid alter '%1'").arg(alter), &e);
                bool ok2;
                const double altervalue = alter.toDouble(&ok2);
                if (ok2 && (std::abs(altervalue) < 2.0) && (m_accType == AccidentalType::NONE)) {
                    // try to see if a microtonal accidental is needed
                    m_accType = microtonalGuess(altervalue);

                    // If it's not a microtonal accidental we will use tuning
                    if (m_accType == AccidentalType::NONE) {
                        m_tuning = 100 * altervalue;
                    }
                }
                m_alter = 0;
            }
        } else if (e.name() == "octave") {
            const String oct = e.readText();
            bool ok;
            m_octave = oct.toInt(&ok);
            if (!ok || m_octave < 0 || m_octave > 9) {
                m_logger->logError(String(u"invalid octave '%1'").arg(oct), &e);
                m_octave = -1;
            }
        } else if (e.name() == "step") {
            const String step = e.readText();
            const size_t pos = String(u"CDEFGAB").indexOf(step);
            if (step.size() == 1 && pos < 7) {
                m_step = int(pos);
            } else {
                m_logger->logError(String(u"invalid step '%1'").arg(step), &e);
            }
        } else {
            // TODO skipLogCurrElem();
        }
    }
    //LOGD("pitch step %d alter %d oct %d accid %hhd", step, alter, oct, accid);
}

//---------------------------------------------------------
//   readProperties
//---------------------------------------------------------

/**
 Handle selected child elements of the /score-partwise/part/measure/note node.
 Return true if handled.
 */

void MusicXmlNotePitch::accidental(const pugi::xml_node& node, Score* score)
{
    const bool cautionary = strcmp(node.attribute("cautionary").value(), "yes") == 0;
    const bool editorial = strcmp(node.attribute("editorial").value(), "yes") == 0;
    const bool parentheses = strcmp(node.attribute("parentheses").value(), "yes") == 0;
    const bool noParentheses = strcmp(node.attribute("parentheses").value(), "no") == 0;
    const bool brackets = strcmp(node.attribute("bracket").value(), "yes") == 0;
    const bool noBrackets = strcmp(node.attribute("bracket").value(), "no") == 0;
    const bool smallAccid = strcmp(node.attribute("size").value(), "cue") == 0 || strcmp(node.attribute("size").value(), "grace-cue") == 0;
    const Color accColor = Color(node.attribute("color").value());
    const String smufl = String::fromUtf8(node.attribute("smufl").value());

    const String s = String::fromUtf8(node.child_value());
    const AccidentalType type = musicXmlString2accidentalType(s, smufl);

    if (type != AccidentalType::NONE) {
        m_acc = Factory::createAccidental(score->dummy());
        m_acc->setAccidentalType(type);
        if (cautionary || editorial) { // no way to tell one from the other
            m_acc->setRole(AccidentalRole::USER);
        } // except via the use of parentheses vs. brackets
        if (noParentheses || noBrackets) { // explicitly none wanted
        } else if (parentheses || cautionary) { // set to "yes" or "cautionary" and not set at all
            m_acc->setBracket(AccidentalBracket(AccidentalBracket::PARENTHESIS));
        } else if (brackets || editorial) { // set to "yes" or "editorial" and not set at all
            m_acc->setBracket(AccidentalBracket(AccidentalBracket::BRACKET));
        }
        if (accColor.isValid()) {
            m_acc->setColor(accColor);
        }
        m_acc->setSmall(smallAccid);
    }
}

void MusicXmlNotePitch::displayStepOctave(const pugi::xml_node& node)
{
    for (pugi::xml_node child : node.children()) {
        if (strcmp(child.name(), "display-step") == 0) {
            const String step = String::fromUtf8(child.child_value());
            int pos = static_cast<int>(String(u"CDEFGAB").indexOf(step));
            if (step.size() == 1 && pos >= 0 && pos < 7) {
                m_displayStep = pos;
            } else {
                LOGD("invalid step '%s'", muPrintable(step));
            }
        } else if (strcmp(child.name(), "display-octave") == 0) {
            const String oct = String::fromUtf8(child.child_value());
            bool ok;
            m_displayOctave = oct.toInt(&ok);
            if (!ok || m_displayOctave < 0 || m_displayOctave > 9) {
                LOGD("invalid octave '%s'", muPrintable(oct));
                m_displayOctave = -1;
            }
        }
    }
}

void MusicXmlNotePitch::pitch(const pugi::xml_node& node)
{
    // defaults
    m_step = -1;
    m_alter = 0;
    m_tuning = 0.0;
    m_octave = -1;

    for (pugi::xml_node child : node.children()) {
        if (strcmp(child.name(), "alter") == 0) {
            const String alter = String::fromUtf8(child.child_value());
            bool ok;
            m_alter = MusicXmlSupport::stringToInt(alter, &ok);
            if (!ok || m_alter < -2 || m_alter > 2) {
                m_logger->logError(String(u"invalid alter '%1'").arg(alter));
                bool ok2;
                const double altervalue = alter.toDouble(&ok2);
                if (ok2 && (std::abs(altervalue) < 2.0) && (m_accType == AccidentalType::NONE)) {
                    m_accType = microtonalGuess(altervalue);
                    if (m_accType == AccidentalType::NONE) {
                        m_tuning = 100 * altervalue;
                    }
                }
                m_alter = 0;
            }
        } else if (strcmp(child.name(), "octave") == 0) {
            const String oct = String::fromUtf8(child.child_value());
            bool ok;
            m_octave = oct.toInt(&ok);
            if (!ok || m_octave < 0 || m_octave > 9) {
                m_logger->logError(String(u"invalid octave '%1'").arg(oct));
                m_octave = -1;
            }
        } else if (strcmp(child.name(), "step") == 0) {
            const String step = String::fromUtf8(child.child_value());
            const size_t pos = String(u"CDEFGAB").indexOf(step);
            if (step.size() == 1 && pos < 7) {
                m_step = int(pos);
            } else {
                m_logger->logError(String(u"invalid step '%1'").arg(step));
            }
        }
    }
}

bool MusicXmlNotePitch::readProperties(muse::XmlStreamReader& e, Score* score)
{
    const AsciiStringView tag(e.name());

    if (tag == "accidental") {
        m_acc = iex::musicxml::accidental(e, score);
        return true;
    } else if (tag == "unpitched") {
        m_unpitched = true;
        displayStepOctave(e);
        return true;
    } else if (tag == "pitch") {
        pitch(e);
        return true;
    }
    return false;
}

bool MusicXmlNotePitch::readProperties(const pugi::xml_node& node, Score* score)
{
    const char* tag = node.name();

    if (strcmp(tag, "accidental") == 0) {
        accidental(node, score);
        return true;
    } else if (strcmp(tag, "unpitched") == 0) {
        m_unpitched = true;
        displayStepOctave(node);
        return true;
    } else if (strcmp(tag, "pitch") == 0) {
        pitch(node);
        return true;
    }
    return false;
}
}
