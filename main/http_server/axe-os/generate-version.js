const fs = require('fs');
const path = require('path');

// ESP-IDF esp_app_desc_t.version is char[32] (31 chars + null terminator).
// Truncate to match so the web and firmware versions always agree.
const ESP_APP_DESC_VERSION_MAX = 31;
const version = require('child_process').execSync('git describe --tags --always --dirty').toString().trim().slice(0, ESP_APP_DESC_VERSION_MAX);

const outputPath = path.join(__dirname, 'dist', 'axe-os', 'version.txt');
fs.writeFileSync(outputPath, version);

console.log(`Generated ${outputPath} with version ${version}`);
