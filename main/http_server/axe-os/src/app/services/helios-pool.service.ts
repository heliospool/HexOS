import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { forkJoin, Observable, of } from 'rxjs';
import { catchError, delay, map } from 'rxjs/operators';
import { environment } from '../../environments/environment';

export type HeliosRegion = 'NA' | 'EU' | 'APAC';

const REGION_LABELS: Record<string, HeliosRegion> = { com: 'NA', eu: 'EU', asia: 'APAC' };

export interface HeliosWorker {
  workername: string;
  hashrate1m: string;
  hashrate5m: string;
  hashrate1hr: string;
  hashrate1d: string;
  hashrate7d: string;
  lastshare: number;
  started: number;
  bestshare: number;
  bestever: number;
  useragent: string;
  shares: number;
}

export interface TaggedHeliosWorker extends HeliosWorker {
  region: HeliosRegion;
}

export interface HeliosRegionData {
  region: HeliosRegion;
  stats: HeliosAccountStats;
  workers: TaggedHeliosWorker[];
}

export interface HeliosAccountStats {
  hashrate1m: string;
  hashrate5m: string;
  hashrate1hr: string;
  hashrate1d: string;
  hashrate7d: string;
  workers: number;
  authorised: number;
  bestshare: number;
  bestever: number;
  shares: number;
  worker: HeliosWorker[];
}

@Injectable({ providedIn: 'root' })
export class HeliosPoolService {
  constructor(private http: HttpClient) {}

  static readonly REGIONS = ['com', 'eu', 'asia'] as const;

  private regionFromStratumUrl(stratumUrl: string): string {
    const lower = stratumUrl.toLowerCase();
    if (lower.includes('heliospool.eu')) return 'eu';
    if (lower.includes('heliospool.asia')) return 'asia';
    return 'com';
  }

  /** Fetches account stats from all known regions in parallel, one entry per region that responds. */
  getAccountStatsAllRegions(coin: 'btc' | 'bch', address: string): Observable<HeliosRegionData[]> {
    if (!environment.production) {
      return this.getAccountStats(coin, address, '').pipe(
        map(s => s ? [{ region: 'NA' as HeliosRegion, stats: s, workers: s.worker.map(w => ({ ...w, region: 'NA' as HeliosRegion })) }] : [])
      );
    }
    return forkJoin(
      HeliosPoolService.REGIONS.map(region =>
        this.http.get<HeliosAccountStats>(`https://${coin}.heliospool.${region}/api/users/${address}`).pipe(
          catchError(() => of(null)),
          map(s => {
            if (!s || !Array.isArray(s.worker) || s.workers === 0) return null;
            const workers = s.worker
              .filter(w => w.started > 0)
              .map(w => ({ ...w, region: REGION_LABELS[region] }));
            return { region: REGION_LABELS[region], stats: s, workers } as HeliosRegionData;
          })
        )
      )
    ).pipe(
      map(results => results.filter((r): r is HeliosRegionData => r !== null))
    );
  }

  getAccountStats(coin: 'btc' | 'bch', address: string, stratumUrl: string): Observable<HeliosAccountStats | null> {
    if (!environment.production) {
      const now = Math.floor(Date.now() / 1000);
      const mockStats: HeliosAccountStats = {
        hashrate1m: '1050G',
        hashrate5m: '1045G',
        hashrate1hr: '1041G',
        hashrate1d: '1038G',
        hashrate7d: '1022G',
        workers: 1,
        authorised: now - 86400 * 30,
        bestshare: 238214491,
        bestever: 238214491,
        shares: 14823,
        worker: [{
          workername: `${address}.hexos-gamma`,
          hashrate1m: '1050G',
          hashrate5m: '1045G',
          hashrate1hr: '1041G',
          hashrate1d: '1038G',
          hashrate7d: '1022G',
          lastshare: now - 47,
          started: now - 7254,
          bestshare: 238214491,
          bestever: 238214491,
          useragent: 'HexOS/v2.13.1-hexos.1/BM1370',
          shares: 14823,
        }]
      };
      return of(mockStats).pipe(delay(400));
    }

    const region = this.regionFromStratumUrl(stratumUrl);
    return this.http.get<HeliosAccountStats>(`https://${coin}.heliospool.${region}/api/users/${address}`).pipe(
      catchError(() => of(null))
    );
  }
}
