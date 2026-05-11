import { HttpClient, HttpEvent } from '@angular/common/http';
import { Injectable, Optional } from '@angular/core';
import { BehaviorSubject, delay, Observable, of, timeout } from 'rxjs';
import { eChartLabel } from 'src/models/enum/eChartLabel';
import { chartLabelKey } from 'src/models/enum/eChartLabel';
import { chartLabelValue } from 'src/models/enum/eChartLabel';
import {
  SystemInfo as ISystemInfo,
  SystemStatistics as ISystemStatistics,
  SystemASIC as ISystemASIC,
  SystemService as GeneratedSystemService,
  Settings
} from 'src/app/generated';

import { environment } from '../../environments/environment';

const API_TIMEOUT = 15000;

/** Jitter a number by up to ±pct percent */
function jitter(val: number, pct: number): number {
  return val * (1 + (Math.random() * 2 - 1) * pct / 100);
}

/** Drift a number toward target by step, clamp to [min, max] */
function drift(val: number, target: number, step: number, min: number, max: number): number {
  const d = target - val;
  const move = Math.min(Math.abs(d), step) * Math.sign(d);
  return Math.min(max, Math.max(min, val + move + (Math.random() - 0.5) * step));
}

const MOCK_SETTINGS_KEY = 'axe_mock_settings';

function loadMockSettings(): void {
  try {
    const raw = localStorage.getItem(MOCK_SETTINGS_KEY);
    if (raw) Object.assign(environment.mockData.systemInfo, JSON.parse(raw));
  } catch { /* ignore */ }
}

function saveMockSettings(update: Record<string, any>): void {
  try {
    const raw = localStorage.getItem(MOCK_SETTINGS_KEY);
    const existing = raw ? JSON.parse(raw) : {};
    localStorage.setItem(MOCK_SETTINGS_KEY, JSON.stringify({ ...existing, ...update }));
  } catch { /* ignore */ }
}

let mockSettingsLoaded = false;

function tickMockData() {
  const m = environment.mockData.systemInfo as any;
  if (!m) return;

  m.uptimeSeconds = (m.uptimeSeconds ?? 0) + 5;

  const nominalHash = 8577;
  m.hashRate      = Math.round(jitter(m.hashRate ?? nominalHash, 1.2));
  m.hashRate_1m   = Math.round(jitter(m.hashRate_1m ?? nominalHash, 0.8));
  m.hashRate_10m  = Math.round(jitter(m.hashRate_10m ?? nominalHash, 0.5));
  m.hashRate_1h   = Math.round(jitter(m.hashRate_1h ?? nominalHash, 0.3));

  const nominalEff = 6.80;
  m.efficiency_1m  = parseFloat(jitter(m.efficiency_1m  ?? nominalEff, 0.8).toFixed(2));
  m.efficiency_10m = parseFloat(jitter(m.efficiency_10m ?? nominalEff, 0.5).toFixed(2));
  m.efficiency_1h  = parseFloat(jitter(m.efficiency_1h  ?? nominalEff, 0.3).toFixed(2));

  const nominalDiff = 11500;
  m.diff_1m  = Math.round(jitter(m.diff_1m  ?? nominalDiff, 2000));
  m.diff_10m = Math.round(jitter(m.diff_10m ?? nominalDiff, 1000));
  m.diff_1h  = Math.round(jitter(m.diff_1h  ?? nominalDiff, 500));

  m.temp    = parseFloat(drift(m.temp ?? 67, 67, 0.4, 55, 74).toFixed(1));
  m.temp2   = parseFloat(drift(m.temp2 ?? 65, 65, 0.3, 54, 72).toFixed(1));
  m.vrTemp  = parseFloat(drift(m.vrTemp ?? 54, 54, 0.3, 48, 60).toFixed(1));
  m.boardTemp = parseFloat(drift(m.boardTemp ?? 42.5, 42.5, 0.2, 38, 48).toFixed(1));

  const tempRatio = Math.min(1, Math.max(0, (m.temp - 50) / 25));
  const targetFan = Math.round(40 + tempRatio * 55);
  m.fanspeed = Math.min(100, Math.max(25, Math.round(drift(m.fanspeed ?? 72, targetFan, 2, 25, 100))));
  m.fanrpm   = Math.round(m.fanspeed * 58 + (Math.random() - 0.5) * 150);

  m.power   = parseFloat(jitter(m.power ?? 58.2, 0.8).toFixed(1));
  m.current = Math.round(jitter(m.current ?? 4810, 0.5));

  const shareInterval = 5 / 30; // ~1 share per 30s
  if (Math.random() < shareInterval) {
    m.sharesAccepted = (m.sharesAccepted ?? 0) + 1;
    m.lastAcceptedShareSeconds = 0;
  } else {
    m.lastAcceptedShareSeconds = Math.min((m.lastAcceptedShareSeconds ?? 0) + 5, 120);
  }
  m.asicLastJobSeconds = Math.min((m.asicLastJobSeconds ?? 0) + 5, 30);

  m.responseTime = parseFloat(jitter(m.responseTime ?? 18, 15).toFixed(1));

  m.errorPercentage = parseFloat(Math.min(2, Math.max(0, jitter(m.errorPercentage ?? 0.4, 5))).toFixed(2));

  if (m.hashrateMonitor?.asics) {
    m.hashrateMonitor = {
      ...m.hashrateMonitor,
      asics: m.hashrateMonitor.asics.map((a: any) => {
        const chipTotal = parseFloat(jitter(a.total, 1.5).toFixed(1));
        return {
          ...a,
          total: chipTotal,
          chipTemp: parseFloat(drift(a.chipTemp, a.chipTemp, 0.3, 55, 80).toFixed(1)),
        };
      }),
      hashrate: m.hashRate,
    };
  }

  m.freeHeap = Math.max(180000, (m.freeHeap ?? 200504) - Math.round(Math.random() * 200));
  if (m.freeHeap < 185000) m.freeHeap = 210000 + Math.round(Math.random() * 10000);

  // Simulate a block find roughly once every ~5 minutes of mock uptime
  if (!m.showNewBlock && Math.random() < (5 / (5 * 60))) {
    m.blockFound = (m.blockFound ?? 0) + 1;
    m.showNewBlock = true;
    m.lastBlockTime = Math.floor(Date.now() / 1000);
    m.lastBlockUrl = m.stratumURL ?? 'btc.heliospool.com';
    m.lastBlockDiff = m.lastSubmittedDiff ?? 3891;
    m.lastBlockNetDiff = m.networkDifficulty ?? 155970000000000;
  }
}

@Injectable({
  providedIn: 'root'
})
export class SystemApiService {

  public latestInfo$ = new BehaviorSubject<ISystemInfo | null>(null);

  constructor(
    private httpClient: HttpClient,
    @Optional() private generatedSystemService: GeneratedSystemService
  ) {
    if (!environment.production && environment.mockData) {
      if (!mockSettingsLoaded) { loadMockSettings(); mockSettingsLoaded = true; }
      this.latestInfo$.next({ ...environment.mockData.systemInfo } as ISystemInfo);
    }
  }

  public getInfo(uri: string = ''): Observable<ISystemInfo> {
    if (environment.production && this.generatedSystemService && !uri) {
      return this.generatedSystemService.getSystemInfo().pipe(timeout(API_TIMEOUT));
    }

    if (environment.production && uri) {
      return this.httpClient.get<ISystemInfo>(`${uri}/api/system/info`).pipe(timeout(API_TIMEOUT));
    }

    if (!mockSettingsLoaded) { loadMockSettings(); mockSettingsLoaded = true; }
    tickMockData();
    return of(
      { ...environment.mockData.systemInfo } as ISystemInfo
    ).pipe(delay(300));
  }

  public getStatistics(y1: string, y2: string, uri: string = ''): Observable<ISystemStatistics> {
    let columnList = [chartLabelKey(eChartLabel.hashrate), chartLabelKey(eChartLabel.power)];

    if ((y1 != chartLabelKey(eChartLabel.hashrate)) && (y1 != chartLabelKey(eChartLabel.power))) {
      columnList.push(y1);
    }
    if ((y2 != chartLabelKey(eChartLabel.hashrate)) && (y2 != chartLabelKey(eChartLabel.power))) {
      columnList.push(y2);
    }

    if (environment.production && this.generatedSystemService) {
      return this.generatedSystemService.getSystemStatistics(columnList).pipe(timeout(API_TIMEOUT));
    }

        const hashrateData = environment.mockData.statistics.hashrate;
    const powerData = environment.mockData.statistics.power;
    const asicTempData = [-1,58.5,59.625,60.125,60.75,61.5,61.875,62.125,62.5,63];
    const vrTempData = [45,45,45,44,45,44,44,45,45,45];
    const asicVoltageData = [1221,1223,1219,1223,1217,1222,1221,1219,1221,1221];
    const voltageData = [5196.875,5204.6875,5196.875,5196.875,5196.875,5196.875,5196.875,5196.875,5196.875,5204.6875];
    const currentData = [2284.375,2284.375,2253.125,2284.375,2253.125,2231.25,2284.375,2253.125,2253.125,2284.375];
    const fanSpeedData = [48,52,50,52,53,54,50,50,48,48];
    const fanRpmData = [4032,3545,3904,3691,3564,3554,3691,3573,3701,4044];
    const fan2RpmData = [3545,3904,3691,3564,3554,3691,3573,3701,4044, 4032];
    const wifiRssiData = [-35,-34,-33,-34,-34,-34,-33,-35,-33,-34];
    const freeHeapData = [214504,212504,213504,210504,207504,209504,203504,202504,201504,200504];
    const responseTimeData = [15.1,14.5,14.3,15.1,13.1,16.1,28.6,18.4,17.7,17.6,18.0,15.5];
    const timestampData = [13131,18126,23125,28125,33125,38125,43125,48125,53125,58125];

    columnList.push("timestamp");
    let statisticsList: number[][] = [];

    for(let i: number = 0; i < 10; i++) {
      statisticsList[i] = [];
      for(let j: number = 0; j < columnList.length; j++) {
        switch (chartLabelValue(columnList[j])) {
          case eChartLabel.hashrate:     statisticsList[i][j] = hashrateData[i];     break;
          case eChartLabel.hashrate_1m:  statisticsList[i][j] = hashrateData[i];     break;
          case eChartLabel.hashrate_10m: statisticsList[i][j] = hashrateData[i];     break;
          case eChartLabel.hashrate_1h:  statisticsList[i][j] = hashrateData[i];     break;
          case eChartLabel.power:        statisticsList[i][j] = powerData[i];        break;
          case eChartLabel.asicTemp:     statisticsList[i][j] = asicTempData[i];     break;
          case eChartLabel.vrTemp:       statisticsList[i][j] = vrTempData[i];       break;
          case eChartLabel.asicVoltage:  statisticsList[i][j] = asicVoltageData[i];  break;
          case eChartLabel.voltage:      statisticsList[i][j] = voltageData[i];      break;
          case eChartLabel.current:      statisticsList[i][j] = currentData[i];      break;
          case eChartLabel.fanSpeed:     statisticsList[i][j] = fanSpeedData[i];     break;
          case eChartLabel.fanRpm:       statisticsList[i][j] = fanRpmData[i];       break;
          case eChartLabel.fan2Rpm:      statisticsList[i][j] = fan2RpmData[i];      break;
          case eChartLabel.wifiRssi:     statisticsList[i][j] = wifiRssiData[i];     break;
          case eChartLabel.freeHeap:     statisticsList[i][j] = freeHeapData[i];     break;
          case eChartLabel.responseTime: statisticsList[i][j] = responseTimeData[i]; break;
          default:
            if (columnList[j] === "timestamp") {
              statisticsList[i][j] = timestampData[i];
            } else {
              statisticsList[i][j] = 0;
            }
            break;
        }
      }
    }

    return of({
      currentTimestamp: 61125,
      labels: columnList,
      statistics: statisticsList
    });
  }

  public restart(uri: string = '') {
    if (environment.production && this.generatedSystemService && !uri) {
      return this.generatedSystemService.restartSystem();
    }

    if (environment.production && uri) {
      return this.httpClient.post(`${uri}/api/system/restart`, {});
    }

    return of('Device restarted (mock)');
  }

  public dismissBlockFound(uri: string = '') {
    if (environment.production && this.generatedSystemService && !uri) {
      return this.generatedSystemService.dismissBlockFound();
    }

    if (environment.production && uri) {
      return this.httpClient.post(`${uri}/api/system/blockFound/dismiss`, {});
    }

    const m = environment.mockData.systemInfo as any;
    m.blockFound = 0;
    m.showNewBlock = false;
    return of({ blockFound: 0, showNewBlock: false });
  }

  public resetBlocks(uri: string = '') {
    if (environment.production && !uri) {
      return this.httpClient.post('/api/system/blockFound/reset', {});
    }

    if (environment.production && uri) {
      return this.httpClient.post(`${uri}/api/system/blockFound/reset`, {});
    }

    const m = environment.mockData.systemInfo as any;
    m.blockFound = 0;
    m.lifetimeBlocksFound = 0;
    m.showNewBlock = false;
    m.lastBlockTime = 0;
    m.lastBlockDiff = 0;
    m.lastBlockNetDiff = 0;
    m.lastBlockUrl = '';
    return of({ message: 'Block data reset' });
  }

  public getSystemDefaults(): Observable<Record<string, any>> {
    if (environment.production) {
      return this.httpClient.get<Record<string, any>>('/api/system/defaults');
    }
    return of({});
  }

  public identify(uri: string = '') {
    if (environment.production && this.generatedSystemService && !uri) {
      return this.generatedSystemService.identifySystem();
    }

    if (environment.production && uri) {
      return this.httpClient.post(`${uri}/api/system/identify`, {});
    }

    return of('Device identified (mock)');
  }

  public updateSystem(uri: string = '', update: any) {
    if (environment.production && this.generatedSystemService && !uri) {
      return this.generatedSystemService.updateSystemSettings(update as Settings);
    }

    if (environment.production && uri) {
      return this.httpClient.patch(`${uri}/api/system`, update);
    }

    Object.assign(environment.mockData.systemInfo, update);
    saveMockSettings(update);
    return of(true);
  }

  private otaUpdate(file: File | Blob, url: string): Observable<HttpEvent<string>> {
    return new Observable<HttpEvent<string>>((subscriber) => {
      const reader = new FileReader();

      reader.onload = (event: any) => {
        const fileContent = event.target.result;

        this.httpClient.post(url, fileContent, {
          reportProgress: true,
          observe: 'events',
          responseType: 'text',
          headers: {
            'Content-Type': 'application/octet-stream',
          },
        }).subscribe({
          next: (event) => {
            subscriber.next(event);
          },
          error: (err) => {
            subscriber.error(err)
          },
          complete: () => {
            subscriber.complete();
          }
        });
      };
      reader.readAsArrayBuffer(file);
    });
  }

  public performOTAUpdate(file: File | Blob): Observable<HttpEvent<string>> {
    if (environment.production && this.generatedSystemService) {
      return this.generatedSystemService.updateFirmware(file, 'events', true);
    }
    return this.otaUpdate(file, '/api/system/OTA');
  }

  public performWWWOTAUpdate(file: File | Blob): Observable<HttpEvent<string>> {
    if (environment.production && this.generatedSystemService) {
      return this.generatedSystemService.updateWebInterface(file, 'events', true);
    }
    return this.otaUpdate(file, '/api/system/OTAWWW');
  }

  public getAsicSettings(uri: string = ''): Observable<ISystemASIC> {
    if (environment.production && this.generatedSystemService && !uri) {
      return this.generatedSystemService.getAsicSettings().pipe(timeout(API_TIMEOUT));
    }

    if (environment.production && uri) {
      return this.httpClient.get<ISystemASIC>(`${uri}/api/system/asic`).pipe(timeout(API_TIMEOUT));
    }

    return of(environment.mockData.asicSettings as ISystemASIC).pipe(delay(1000));
  }

  private mockProfiles: any[] = [];
  private mockProfileNextId = 0;

  public getProfiles(uri: string = ''): Observable<any[]> {
    if (environment.production) {
      return this.httpClient.get<any[]>(`${uri}/api/system/profiles`).pipe(timeout(API_TIMEOUT));
    }
    return of([...this.mockProfiles]);
  }

  public saveProfile(profile: any, uri: string = ''): Observable<any> {
    if (environment.production) {
      return this.httpClient.post<any>(`${uri}/api/system/profiles`, profile).pipe(timeout(API_TIMEOUT));
    }
    const id = this.mockProfileNextId++;
    this.mockProfiles.push({ ...profile, id });
    return of({ id });
  }

  public deleteProfile(id: number, uri: string = ''): Observable<any> {
    if (environment.production) {
      return this.httpClient.delete<any>(`${uri}/api/system/profiles`, { body: { id } }).pipe(timeout(API_TIMEOUT));
    }
    this.mockProfiles = this.mockProfiles.filter(p => p.id !== id);
    return of({});
  }

  public updateProfile(id: number, profile: any, uri: string = ''): Observable<any> {
    if (environment.production) {
      return this.httpClient.patch<any>(`${uri}/api/system/profiles`, { ...profile, id }).pipe(timeout(API_TIMEOUT));
    }
    const idx = this.mockProfiles.findIndex(p => p.id === id);
    if (idx !== -1) this.mockProfiles[idx] = { ...profile, id };
    return of({ id });
  }

  private mockPoolProfiles: any[] = [];
  private mockPoolProfileNextId = 0;

  public getPoolProfiles(uri: string = ''): Observable<any[]> {
    if (environment.production) {
      return this.httpClient.get<any[]>(`${uri}/api/system/pool-profiles`).pipe(timeout(API_TIMEOUT));
    }
    return of([...this.mockPoolProfiles]);
  }

  public savePoolProfile(preset: any, uri: string = ''): Observable<any> {
    if (environment.production) {
      return this.httpClient.post<any>(`${uri}/api/system/pool-profiles`, preset).pipe(timeout(API_TIMEOUT));
    }
    const id = this.mockPoolProfileNextId++;
    this.mockPoolProfiles.push({ ...preset, id });
    return of({ id });
  }

  public deletePoolProfile(id: number, uri: string = ''): Observable<any> {
    if (environment.production) {
      return this.httpClient.delete<any>(`${uri}/api/system/pool-profiles`, { body: { id } }).pipe(timeout(API_TIMEOUT));
    }
    this.mockPoolProfiles = this.mockPoolProfiles.filter(p => p.id !== id);
    return of({});
  }

  public updatePoolProfile(id: number, preset: any, uri: string = ''): Observable<any> {
    if (environment.production) {
      return this.httpClient.patch<any>(`${uri}/api/system/pool-profiles`, { ...preset, id }).pipe(timeout(API_TIMEOUT));
    }
    const idx = this.mockPoolProfiles.findIndex(p => p.id === id);
    if (idx !== -1) this.mockPoolProfiles[idx] = { ...preset, id };
    return of({ id });
  }

}
