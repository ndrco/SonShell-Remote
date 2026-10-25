#!/usr/bin/env python3
"""Построить подписи параметров записи из локального Sony SDK, без копии SDK в Git."""
import argparse
from pathlib import Path
import re

ENUMS = {
    'FileType': ('CrFileType', 'CrFileType_'),
    'CompressionFileFormatStill': ('CrCompressionFileFormat', 'CrCompressionFileFormat_'),
    'StillImageQuality': ('CrImageQuality', 'CrImageQuality_'),
    'ImageSize': ('CrImageSize', 'CrImageSize_'),
    'AspectRatio': ('CrAspectRatioIndex', 'CrAspectRatio_'),
    'RAW_FileCompressionType': ('CrRAWFileCompressionType', 'CrRAWFile_'),
    'ImageStabilizationSteadyShot': ('CrImageStabilizationSteadyShot', 'CrImageStabilizationSteadyShot_'),
    'Movie_ImageStabilizationSteadyShot': ('CrImageStabilizationSteadyShotMovie', 'CrImageStabilizationSteadyShotMovie_'),
    'Movie_File_Format': ('CrFileFormatMovie', 'CrFileFormatMovie_'),
    'Movie_Recording_FrameRateSetting': ('CrRecordingFrameRateSettingMovie', 'CrRecordingFrameRateSettingMovie_'),
    'Movie_Recording_Setting': ('CrRecordingSettingMovie', 'CrRecordingSettingMovie_'),
}


def label(name, property_name):
    if property_name == 'AspectRatio':
        return name.replace('_', ':')
    if property_name == 'Movie_Recording_FrameRateSetting':
        return name.replace('_', '.')
    words = {'None': 'NONE', 'Raw': 'RAW', 'RawJpeg': 'RAW+JPEG', 'Jpeg': 'JPEG',
             'RawHeif': 'RAW+HEIF', 'Heif': 'HEIF', 'ExFine': 'Extra fine',
             'Uncompression': 'Uncompressed', 'Compression': 'Compressed',
             'LossLess': 'Lossless', 'LossLessL': 'Lossless L', 'LossLessM': 'Lossless M',
             'LossLessS': 'Lossless S', 'Compressed_HQ': 'Compressed HQ',
             'DynamicActive': 'Dynamic active', 'HEIF_422': 'HEIF 4:2:2', 'HEIF_420': 'HEIF 4:2:0'}
    if name in words:
        return words[name]
    text = name.replace('_', ' ')
    text = re.sub(r'\b(\d+)M\b', r'\1 Mbps', text)
    text = re.sub(r'\b(\d+)bit\b', r'\1-bit', text)
    return re.sub(r'\b(422|420)\b', lambda m: {'422': '4:2:2', '420': '4:2:0'}[m[0]], text)


def generate(header):
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', header, flags=re.S)
    lines = ['// Generated from the local SDK. Do not edit or commit this output.',
             '#pragma once', '#include <cstdint>', '#include <sstream>', '#include <string>',
             'inline std::string recording_control_label(unsigned code, std::uint64_t value) {',
             '  switch (code) {']
    for property_name, (enum, prefix) in ENUMS.items():
        match = re.search(r'\benum\s+' + enum + r'\s*:[^{]+\{([^}]+)\}', source)
        if not match:
            raise ValueError('Required SDK enum is missing: ' + enum)
        names = re.findall(r'\b' + prefix + r'(\w+)\s*(?:=[^,]+)?(?=,|$)', match[1].strip())
        if not names:
            raise ValueError('SDK enum has no values: ' + enum)
        lines += ['    case SCRSDK::CrDeviceProperty_' + property_name + ':', '      switch (value) {']
        lines += ['        case SCRSDK::' + prefix + name + ': return "' + label(name, property_name) + '";' for name in names]
        lines += ['      }', '      break;']
    lines += ['    default: return {};', '  }',
              '  std::ostringstream text; text << "0x" << std::hex << value; return text.str();', '}', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--header', type=Path, required=True)
    parser.add_argument('-o', type=Path, required=True)
    args = parser.parse_args()
    args.o.parent.mkdir(parents=True, exist_ok=True)
    args.o.write_text(generate(args.header.read_text()), encoding='utf-8')
