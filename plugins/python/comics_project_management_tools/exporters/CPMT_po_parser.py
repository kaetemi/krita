"""
Copyright (c) 2018 Wolthera van Hövell tot Westerflier <griffinvalley@gmail.com>

This file is part of the Comics Project Management Tools(CPMT).

CPMT is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

CPMT is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the CPMT.  If not, see <http://www.gnu.org/licenses/>.
"""

"""
A thing that parses through POT files.
"""

import sys
import os

class po_file_parser():
    translationDict = {}
    translationList = []

    def __init__(self, translationLocation):
        for entry in os.scandir(translationLocation):
            if entry.name.endswith('.po') and entry.is_file():
                self.parse_pot(os.path.join(translationLocation, entry.name))

    def parse_pot(self, location):
        if (os.path.exists(location)):
            file = open(location, "r", newline="", encoding="utf8")
            lang = "en"
            for line in file:
                if line.startswith("\"Language: "):
                    lang = line[len("\"Language: "):]
                    lang = lang.replace('\\n"\n', "")
            file.close()
            file = open(location, "r", newline="", encoding="utf8")
            multiLine = ""
            key = None
            entry = {}
            for line in file:
                if line.isspace():
                    if len(entry.keys())>0:
                        if key is None:
                            key = entry.get("text", None)
                            entry.pop("text")
                        if key is not None:
                            if len(key)>0:
                                dummyDict = {}
                                dummyDict = self.translationDict.get(key, dummyDict)
                                dummyDict[lang] = entry
                                self.translationDict[key] = dummyDict
                    entry = {}
                    key = None
                    multiLine = ""
                if line.startswith("msgid "):
                    string = line.strip("msgid \"")
                    string = string[:-len('"\n')]
                    string = string.replace("\\\"", "\"")
                    string = string.replace("\\\'", "\'")
                    string = string.replace("\\#", "#")
                    entry["text"] = string
                    multiLine = "text"
                if line.startswith("msgstr "):
                    string = line.strip("msgstr \"")
                    string = string[:-len('"\n')]
                    string = string.replace("\\\"", "\"")
                    string = string.replace("\\\'", "\'")
                    string = string.replace("\\#", "#")
                    entry["trans"] = string
                    multiLine = "trans"
                if line.startswith("# "):
                    #Translator comment
                    if "translComment" in entry.keys():
                        entry["translComment"] += line
                    else:
                        entry["translComment"] = line
                if line.startswith("#. "):
                    entry["extract"] = line
                if line.startswith("msgctxt "):
                    key = line.strip("msgctxt ")
                if line.startswith("\"") and len(multiLine)>0:
                    string = line[1:]
                    string = string[:-len('"\n')]
                    string = string.replace("\\\"", "\"")
                    string = string.replace("\\\'", "\'")
                    string = string.replace("\\#", "#")
                    entry[multiLine] += string
            if len(entry.keys())>0:
                if key is None:
                    key = entry.get("text", None)
                    entry.pop("text")
                if key is not None:
                    if len(key)>0:
                        dummyDict = {}
                        dummyDict = self.translationDict.get(key, dummyDict)
                        dummyDict[lang] = entry
                        self.translationDict[key] = dummyDict
            self.translationList.append(lang)
            file.close()

    def get_translation_list(self):
        return self.translationList
    
    def get_entry_for_key(self, key, lang):
        entry = {}
        entry["trans"] = " "
        if key in self.translationDict.keys():
            translations = {}
            translations = self.translationDict[key]
            if lang not in translations.keys():
                print("language missing")
                return entry
            return translations[lang]
        else:
            print("translation missing from the translated strings")
            return entry
