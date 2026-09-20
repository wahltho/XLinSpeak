import hashlib
from pathlib import Path
import stat
import sys
import tempfile
import unittest
import warnings
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from create_mtk_manifest import create_manifest


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.archive = Path(self.temp.name) / 'XLinSpeak-linux.1.3.1.zip'
        self.plugin = b'\x7fELF\x02\x01' + b'\0' * 12 + b'\x3e\x00' + b'payload'

    def write(self, extra=None, plugin=None):
        with zipfile.ZipFile(self.archive, 'w') as package:
            package.writestr('XLinSpeak/README.md', b'Instructions')
            package.writestr('XLinSpeak/lin_x64/XLinSpeak.xpl', self.plugin if plugin is None else plugin)
            if extra is not None:
                package.writestr(extra, b'extra')

    def test_hashes_sizes_and_contract_match_without_changing_archive(self):
        self.write()
        before = self.archive.read_bytes()
        manifest = create_manifest(self.archive, '1.3.1')
        self.assertEqual(before, self.archive.read_bytes())
        self.assertEqual(manifest['archive']['sha256'], hashlib.sha256(before).hexdigest())
        self.assertEqual(manifest['archive']['size'], len(before))
        self.assertEqual(manifest['supportedPlatforms'], ['linux-x64'])
        self.assertEqual(manifest['targetPath'], 'Resources/plugins/XLinSpeak')
        self.assertEqual(manifest['releaseTag'], 'r1.3.1')
        plugin = next(f for f in manifest['files'] if f['path'].endswith('.xpl'))
        self.assertEqual(plugin['sha256'], hashlib.sha256(self.plugin).hexdigest())
        self.assertEqual(plugin['size'], len(self.plugin))

    def test_rejects_unsafe_paths_and_links(self):
        link = zipfile.ZipInfo('XLinSpeak/link')
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        for entry in ['../escape', 'XLinSpeak/../escape', '/absolute', 'other/file', 'XLinSpeak/a\\b', link]:
            with self.subTest(entry=entry):
                self.write(extra=entry)
                with self.assertRaises(ValueError):
                    create_manifest(self.archive, '1.3.1')

    def test_rejects_duplicate_file(self):
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', UserWarning)
            self.write(extra='XLinSpeak/README.md')
        with self.assertRaises(ValueError):
            create_manifest(self.archive, '1.3.1')

    def test_rejects_other_architecture_and_non_elf(self):
        for plugin in [b'not ELF', self.plugin[:18] + b'\xb7\x00' + self.plugin[20:]]:
            self.write(plugin=plugin)
            with self.assertRaises(ValueError):
                create_manifest(self.archive, '1.3.1')

    def test_rejects_version_mismatch(self):
        self.write()
        for version in ['1.3.2', 'r1.3.1', '1.3.1-beta']:
            with self.assertRaises(ValueError):
                create_manifest(self.archive, version)

    def test_rejects_missing_plugin(self):
        with zipfile.ZipFile(self.archive, 'w') as package:
            package.writestr('XLinSpeak/README.md', 'README only')
        with self.assertRaises(ValueError):
            create_manifest(self.archive, '1.3.1')


if __name__ == '__main__':
    unittest.main()
