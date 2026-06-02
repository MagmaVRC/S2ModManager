const { Resvg } = require('@resvg/resvg-js');
const fs = require('fs');
const path = require('path');

const srcDir = path.join(__dirname, 'node_modules', 'lucide-static', 'icons');
const outDir = path.join(__dirname, '..', '..', 'resources', 'icons');
fs.mkdirSync(outDir, { recursive: true });

const SIZE = 64;
const icons = [
  'search', 'file', 'folder', 'folder-open', 'chevron-up', 'chevron-down',
  'arrow-up', 'arrow-down', 'settings', 'share-2', 'play', 'plus',
  'package', 'box', 'grip-vertical', 'x', 'trash-2',
  'key-round', 'copy', 'check', 'download', 'upload', 'link', 'circle-check',
  'triangle-alert', 'hard-drive', 'send', 'file-down', 'file-up', 'wifi',
  'chevron-left',
];

for (const name of icons) {
  let svg = fs.readFileSync(path.join(srcDir, name + '.svg'), 'utf8');
  svg = svg.replace(/currentColor/g, '#ffffff');
  const r = new Resvg(svg, { fitTo: { mode: 'width', value: SIZE } });
  const png = r.render().asPng();
  const file = name.replace(/-/g, '_') + '.png';
  fs.writeFileSync(path.join(outDir, file), png);
  console.log('wrote', file, png.length, 'bytes');
}
