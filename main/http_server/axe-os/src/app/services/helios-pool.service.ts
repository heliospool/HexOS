import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { Observable, of } from 'rxjs';
import { catchError, delay } from 'rxjs/operators';
import { environment } from '../../environments/environment';

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

  getAccountStats(coin: 'btc' | 'bch', address: string): Observable<HeliosAccountStats | null> {
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

    return this.http.get<HeliosAccountStats>(`https://heliospool.com/api/${coin}/users/${address}`).pipe(
      catchError(() => of(null))
    );
  }
}
