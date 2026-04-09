import { HttpClient, HttpEvent } from '@angular/common/http';
import { Injectable, Optional } from '@angular/core';
import { delay, Observable, of, timeout } from 'rxjs';
import { eChartLabel } from 'src/models/enum/eChartLabel';
import { chartLabelKey } from 'src/models/enum/eChartLabel';
import { chartLabelValue } from 'src/models/enum/eChartLabel';
import {
  SystemInfo as ISystemInfo,
  SystemStatistics as ISystemStatistics,
  SystemASIC as ISystemASIC,
  SystemASICASICModelEnum,
  SystemService as GeneratedSystemService,
  Settings
} from 'src/app/generated';

import { environment } from '../../environments/environment';

const API_TIMEOUT = 15000;

@Injectable({
  providedIn: 'root'
})
export class SystemApiService {

  constructor(
    private httpClient: HttpClient,
    @Optional() private generatedSystemService: GeneratedSystemService
  ) { }

  public getInfo(uri: string = ''): Observable<ISystemInfo> {
    if (environment.production && this.generatedSystemService && !uri) {
      return this.generatedSystemService.getSystemInfo().pipe(timeout(API_TIMEOUT));
    }

    if (environment.production && uri) {
      return this.httpClient.get<ISystemInfo>(`${uri}/api/system/info`).pipe(timeout(API_TIMEOUT));
    }

    return of(
      {
        power: 44.3,
        voltage: 5100,
        current: 28900,
        currentLimit: 30000,
        temp: 62,
        temp2: 58,
        vrTemp: 51,
        maxPower: 40,
        nominalVoltage: 5,
        hashRate: 1050,
        hashRate_1m: 1048,
        hashRate_10m: 1045,
        hashRate_1h: 1041,
        expectedHashrate: 1050,
        errorPercentage: 0.4,
        bestDiff: 238214491,
        bestSessionDiff: 21212121,
        freeHeap: 200504,
        freeHeapInternal: 200504,
        freeHeapSpiram: 200504,
        coreVoltage: 1360,
        coreVoltageActual: 1360,
        hostname: "HexOS",
        macAddr: "2C:54:91:88:C9:E3",
        ssid: "HeliosNet",
        ipv4: "192.168.1.42",
        ipv6: "fe80::62be:b4ff:fe04:ea9c",
        wifiPass: "password",
        wifiStatus: "Connected!",
        wifiRSSI: -48,
        apEnabled: 0,
        sharesAccepted: 312,
        sharesRejected: 3,
        sharesRejectedReasons: [
          { message: "Above target", count: 2 },
          { message: "Duplicate share", count: 1 }
        ],
        uptimeSeconds: 7254,
        smallCoreCount: 2040,
        ASICModel: "BM1370" as SystemASICASICModelEnum,
        stratumURL: "btc.heliospool.com",
        stratumPort: 3333,
        stratumUser: "bc1q99n3pu025yyu0jlywpmwzalyhm36tg5u37w20d.hexos-gamma",
        stratumSuggestedDifficulty: 4096,
        stratumExtranonceSubscribe: !!1,
        stratumTLS: !!0,
        stratumCert: "",
        stratumDecodeCoinbase: 1,
        fallbackStratumURL: "solo.heliospool.com",
        fallbackStratumPort: 3333,
        fallbackStratumUser: "bc1q99n3pu025yyu0jlywpmwzalyhm36tg5u37w20d.hexos-gamma",
        fallbackStratumSuggestedDifficulty: 4096,
        fallbackStratumExtranonceSubscribe: !!1,
        fallbackStratumTLS: !!0,
        fallbackStratumCert: "",
        fallbackStratumDecodeCoinbase: 0,
        poolDifficulty: 4096,
        lastSubmittedDiff: 3891,
        workReceived: 14823,
        boardTemp: 42.5,
        responseTime: 18,
        isUsingFallbackStratum: 0,
        poolConnectionInfo: "IPv4",
        frequency: 1100,
        version: "v2.13.1-hexos.1",
        axeOSVersion: "v2.13.1-hexos.1",
        idfVersion: "v5.5.3",
        resetReason: "Power-on reset",
        boardVersion: "602",
        display: "SSD1306 (128x32)",
        rotation: 0,
        invertscreen: 0,
        displayTimeout: -1,
        autofanspeed: 1,
        isPSRAMAvailable: 1,
        overclockEnabled: 1,
        runningPartition: "factory",
        minFanSpeed: 25,
        fanspeed: 72,
        manualFanSpeed: 70,
        temptarget: 60,
        statsFrequency: 30,
        fanrpm: 4200,
        fan2rpm: 0,

        boardtemp1: 38,
        boardtemp2: 42,
        overheat_mode: 0,

        blockHeight: 895432,
        scriptsig: "..%..h..,H...heliospool.com/HexOS/",
        networkDifficulty: 155970000000000,
        hashrateMonitor: {
          asics: [{
            total: 1050.4,
            domains: [264.1, 258.7, 261.9, 265.7],
            errorCount: 2,
          }],
          hashrate: 1050.4,
        },
        blockFound: 0,
        showNewBlock: false,
        coinbaseOutputs: [{value: 3.125, address: "bc1q99n3pu025yyu0jlywpmwzalyhm36tg5u37w20d", isUserOutput: true}],
        coinbaseValueTotalSatoshis: 312500000,
        coinbaseValueUserSatoshis: 312500000,
      }
    ).pipe(delay(1000));
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

    const hashrateData = [0,413.4903744405481,410.7764830376959,440.100549473198,430.5816012914026,452.5464981767163,414.9564271189586,498.7294609150379,411.1671601439723,491.327834852684];
    const powerData = [14.45068359375,14.86083984375,15.03173828125,15.1171875,15.1171875,15.1513671875,15.185546875,15.27099609375,15.30517578125,15.33935546875];
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

    return of('Block found notification dismissed (mock)');
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

    return of({
      ASICModel: "BM1370" as SystemASICASICModelEnum,
      deviceModel: "Gamma",
      swarmColor: "purple",
      asicCount: 1,
      defaultFrequency: 485,
      frequencyOptions: [400, 425, 450, 475, 485, 500, 525, 550, 575],
      defaultVoltage: 1200,
      voltageOptions: [1100, 1150, 1200, 1250, 1300]
    }).pipe(delay(1000));
  }


}
