#!/usr/bin/env node
// Patches the OpenAPI-generated model files to add HexOS custom fields.
// Run after `generate:api` to restore fields that codegen doesn't know about.

const fs = require('fs');
const path = require('path');

const modelDir = path.join(__dirname, 'src/app/generated/model');

// 1. system-info.ts — append custom fields before closing brace
const systemInfoPath = path.join(modelDir, 'system-info.ts');
const systemInfoPatch = `    stratumProfileId?: number;
    fallbackStratumProfileId?: number;
    efficiency_1m: number;
    efficiency_10m: number;
    efficiency_1h: number;
    diff_1m: number;
    diff_10m: number;
    diff_1h: number;
    selectedProfileId?: number;
    syslogHost?: string;
}`;
let systemInfo = fs.readFileSync(systemInfoPath, 'utf8');
if (!systemInfo.includes('stratumProfileId')) {
    systemInfo = systemInfo.replace(/\}[\s]*$/, systemInfoPatch);
    fs.writeFileSync(systemInfoPath, systemInfo);
    console.log('Patched system-info.ts');
} else {
    console.log('system-info.ts already patched');
}

// 2. models.ts — append pool-profile export if missing
const modelsPath = path.join(modelDir, 'models.ts');
let models = fs.readFileSync(modelsPath, 'utf8');
if (!models.includes('pool-profile')) {
    models = models.trimEnd() + "\nexport * from './pool-profile';\n";
    fs.writeFileSync(modelsPath, models);
    console.log('Patched models.ts');
} else {
    console.log('models.ts already patched');
}

// 3. pool-profile.ts — create if missing
const poolProfilePath = path.join(modelDir, 'pool-profile.ts');
if (!fs.existsSync(poolProfilePath)) {
    fs.writeFileSync(poolProfilePath, `export interface PoolProfile {
    id: number;
    name: string;
    stratumURL: string;
    stratumPort: number;
    stratumUser: string;
    stratumPassword?: string;
    stratumTLS?: number;
    stratumCert?: string;
    stratumExtranonceSubscribe?: boolean;
    stratumSuggestedDifficulty?: number;
    stratumDecodeCoinbase?: number;
    fallbackStratumURL?: string;
    fallbackStratumPort?: number;
    fallbackStratumUser?: string;
    fallbackStratumPassword?: string;
    fallbackStratumTLS?: number;
    fallbackStratumCert?: string;
}
`);
    console.log('Created pool-profile.ts');
} else {
    console.log('pool-profile.ts already exists');
}
