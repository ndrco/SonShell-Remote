#!/usr/bin/env python3
"""Validate Qt catalogs against GUI messages and formatting placeholders."""
import collections
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parent.parent
literal = r'"(?:\\.|[^"\\])*"'
call = re.compile(r'QCoreApplication::translate\(\s*"App"\s*,\s*(' + literal + r'(?:\s*' + literal + r')*)\s*\)')
sources = set()
for path in [root / 'gui/main.cpp', *sorted((root / 'gui/src').glob('*.cpp'))]:
    for match in call.finditer(path.read_text()):
        sources.add(''.join(json.loads(part) for part in re.findall(literal, match[1])))
errors = []
for path in sorted((root / 'gui/i18n').glob('*.ts')):
    messages = {}
    for context in ET.parse(path).getroot().findall('context'):
        if context.findtext('name') != 'App':
            continue
        for message in context.findall('message'):
            source, translation = message.findtext('source'), message.find('translation')
            if translation is None or translation.get('type') in ('unfinished', 'obsolete', 'vanished'):
                errors.append(f'{path.name}: unfinished translation: {source!r}')
                continue
            text = ''.join(translation.itertext())
            if not text:
                errors.append(f'{path.name}: empty translation: {source!r}')
            if collections.Counter(re.findall(r'%[1-9][0-9]*|%n|%p', source)) != collections.Counter(re.findall(r'%[1-9][0-9]*|%n|%p', text)):
                errors.append(f'{path.name}: different placeholders: {source!r}')
            messages[source] = text
    for missing in sorted(sources - messages.keys()):
        errors.append(f'{path.name}: missing: {missing!r}')
    print(f'{path.name}: {len(messages)} messages, {len(sources)} used')
for error in errors:
    print(error, file=sys.stderr)
sys.exit(bool(errors))
