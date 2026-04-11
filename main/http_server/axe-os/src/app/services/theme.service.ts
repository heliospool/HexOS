import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { environment } from '../../environments/environment';
import { BehaviorSubject, Observable, of } from 'rxjs';
import { catchError, tap } from 'rxjs/operators';

export interface ThemeSettings {
  colorScheme: string;
  accentColors?: {
    [key: string]: string;
  };
}

@Injectable({
  providedIn: 'root'
})
export class ThemeService {
  private readonly mockSettings: ThemeSettings = {
    colorScheme: 'dark',
    accentColors: {
      '--primary-color': '#4caf50',
      '--primary-color-text': '#ffffff',
      '--highlight-bg': '#4caf50',
      '--highlight-text-color': '#ffffff',
      '--focus-ring': '0 0 0 0.2rem rgba(76,175,80,0.2)',
      // PrimeNG Slider
      '--slider-bg': '#dee2e6',
      '--slider-range-bg': '#4caf50',
      '--slider-handle-bg': '#4caf50',
      // Progress Bar
      '--progressbar-bg': '#dee2e6',
      '--progressbar-value-bg': '#4caf50',
      // PrimeNG Checkbox
      '--checkbox-border': '#4caf50',
      '--checkbox-bg': '#4caf50',
      '--checkbox-hover-bg': '#43a047',
      // PrimeNG Button
      '--button-bg': '#4caf50',
      '--button-hover-bg': '#43a047',
      '--button-focus-shadow': '0 0 0 2px #ffffff, 0 0 0 4px #4caf50',
      // Toggle button
      '--togglebutton-bg': '#4caf50',
      '--togglebutton-border': '1px solid #4caf50',
      '--togglebutton-hover-bg': '#43a047',
      '--togglebutton-hover-border': '1px solid #43a047',
      '--togglebutton-text-color': '#ffffff'
    }
  };

  private themeSettingsSubject = new BehaviorSubject<ThemeSettings>(this.mockSettings);
  private themeSettings$ = this.themeSettingsSubject.asObservable();

  constructor(private http: HttpClient) {
    if (environment.production) {
      this.http.get<ThemeSettings>('/api/theme').pipe(
        catchError(() => of(this.mockSettings)),
        tap(settings => this.themeSettingsSubject.next(settings))
      ).subscribe();
    }
  }

  getThemeSettings(): Observable<ThemeSettings> {
    return this.themeSettings$;
  }

  saveThemeSettings(settings: ThemeSettings): Observable<void> {
    if (environment.production) {
      return this.http.post<void>('/api/theme', settings).pipe(
        tap(() => this.themeSettingsSubject.next(settings))
      );
    } else {
      this.themeSettingsSubject.next(settings);
      return of(void 0);
    }
  }
}
