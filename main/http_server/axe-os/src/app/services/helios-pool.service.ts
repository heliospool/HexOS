import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { Observable, of } from 'rxjs';
import { catchError } from 'rxjs/operators';

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
    return this.http.get<HeliosAccountStats>(`https://heliospool.com/api/${coin}/users/${address}`).pipe(
      catchError(() => of(null))
    );
  }
}
