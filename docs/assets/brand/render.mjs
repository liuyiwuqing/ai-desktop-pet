/** Export the checked-in SVG originals. Authoring-only dependency: sharp. */
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';

const require = createRequire(import.meta.url);
const sharp = require(process.argv[2] || 'sharp');
const directory = fileURLToPath(new URL('.', import.meta.url));

for (const size of [64, 256, 1024]) {
  const output = join(directory, `qitomo-icon-${size}.png`);
  await sharp(join(directory, 'qitomo-icon.svg'), { density: 192 })
    .resize(size, size)
    .png()
    .toFile(output);
  console.log(`Exported qitomo-icon-${size}.png (${size} × ${size})`);
}

await sharp(join(directory, 'qitomo-brandboard.svg'), { density: 144 })
  .resize(1440, 960)
  .png()
  .toFile(join(directory, 'qitomo-brandboard.png'));
console.log('Exported qitomo-brandboard.png (1440 × 960)');
