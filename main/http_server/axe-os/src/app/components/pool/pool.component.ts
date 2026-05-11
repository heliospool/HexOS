import { HttpErrorResponse } from '@angular/common/http';
import { Component, Input, OnInit, OnDestroy } from '@angular/core';
import { FormBuilder, FormGroup, Validators, ValidatorFn, ValidationErrors, AbstractControl } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { Subject, merge } from 'rxjs';
import { forkJoin } from 'rxjs';
import { map, takeUntil } from 'rxjs/operators';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemApiService } from 'src/app/services/system.service';
import { PoolProfile } from 'src/app/generated';

type PoolType = 'stratum' | 'fallbackStratum';

interface ITlsOption {
  value: number;
  label: string;
}

@Component({
  selector: 'app-pool',
  templateUrl: './pool.component.html',
  styleUrls: ['./pool.component.scss']
})
export class PoolComponent implements OnInit, OnDestroy {
  public form!: FormGroup;
  public savedChanges: boolean = false;

  public readonly DEFAULT_BITCOIN_ADDRESS = 'bc1qnp980s5fpp8l94p5cvttmtdqy8rvrq74qly2yrfmzkdsntqzlc5qkc4rkq';

  public pools: PoolType[] = ['stratum', 'fallbackStratum'];
  public showPassword = { 'stratum': false, 'fallbackStratum': false };
  public showAdvancedOptions = { 'stratum': false, 'fallbackStratum': false };

  public tlsOptions: ITlsOption[] = [
    { value: 0, label: 'No TLS' },
    { value: 1, label: 'TLS (System certificate)' },
    { value: 2, label: 'TLS (Custom CA certificate)' }
  ];

  public decodeCoinbaseOptions = [
    { value: 3, label: 'Auto' },
    { value: 1, label: 'BTC' },
    { value: 2, label: 'BCH' },
    { value: 0, label: 'Disabled' }
  ];

  // Pool profiles
  public savedProfiles: PoolProfile[] = [];
  public managedProfile: PoolProfile | null = null;
  public selectedPrimaryProfile: PoolProfile | null = null;
  public selectedFallbackProfile: PoolProfile | null = null;
  public isNewProfile = false;
  public activeTabIndex = 0;
  public profileForm!: FormGroup;
  public showProfilePassword = false;

  @Input() uri = '';

  private destroy$ = new Subject<void>();

  constructor(
    private fb: FormBuilder,
    private systemService: SystemApiService,
    private toastr: ToastrService,
    private loadingService: LoadingService
  ) { }

  ngOnInit(): void {
    forkJoin({
      info: this.systemService.getInfo(this.uri),
      profiles: this.systemService.getPoolProfiles(this.uri),
    }).pipe(
      this.loadingService.lockUIUntilComplete(),
      takeUntil(this.destroy$)
    ).subscribe(({ info, profiles }) => {
      this.form = this.fb.group({
        stratumURL: [info.stratumURL, [
          Validators.required,
          Validators.pattern(/^(?!.*stratum\+tcp:\/\/)(?!.*:[1-9]\d{0,4}$).*$/),
        ]],
        stratumPort: [info.stratumPort, [
          Validators.required,
          Validators.pattern(/^[^:]*$/),
          Validators.min(0),
          Validators.max(65535)
        ]],
        stratumExtranonceSubscribe: [info.stratumExtranonceSubscribe == true, [Validators.required]],
        stratumSuggestedDifficulty: [info.stratumSuggestedDifficulty, [Validators.required]],
        stratumUser: [info.stratumUser, [Validators.required]],
        stratumPassword: ['*****', [Validators.required]],
        stratumTLS: [info.stratumTLS || 0],
        stratumCert: [info.stratumCert],
        stratumDecodeCoinbase: [info.stratumDecodeCoinbase ?? 3, [Validators.required]],
        fallbackStratumURL: [info.fallbackStratumURL, [
          Validators.pattern(/^(?!.*stratum\+tcp:\/\/)(?!.*:[1-9]\d{0,4}$).*$/),
        ]],
        fallbackStratumPort: [info.fallbackStratumPort, [
          Validators.required,
          Validators.pattern(/^[^:]*$/),
          Validators.min(0),
          Validators.max(65535)
        ]],
        fallbackStratumExtranonceSubscribe: [info.fallbackStratumExtranonceSubscribe == true, [Validators.required]],
        fallbackStratumSuggestedDifficulty: [info.fallbackStratumSuggestedDifficulty, [Validators.required]],
        fallbackStratumTLS: [info.fallbackStratumTLS || 0],
        fallbackStratumCert: [info.fallbackStratumCert],
        fallbackStratumDecodeCoinbase: [info.fallbackStratumDecodeCoinbase ?? 3, [Validators.required]],
        fallbackStratumUser: [info.fallbackStratumUser, [Validators.required]],
        fallbackStratumPassword: ['*****', [Validators.required]]
      });

      const setupTlsValidation = (tlsControlName: string, certControlName: string) => {
        this.form.get(tlsControlName)?.valueChanges.subscribe(value => {
          const certControl = this.form.get(certControlName);
          if (value === 2) {
            certControl?.setValidators([Validators.required, this.pemCertificateValidator()]);
          } else {
            certControl?.clearValidators();
          }
          certControl?.updateValueAndValidity();
        });
      };
      setupTlsValidation('stratumTLS', 'stratumCert');
      setupTlsValidation('fallbackStratumTLS', 'fallbackStratumCert');
      this.form.get('stratumTLS')?.updateValueAndValidity();
      this.form.get('fallbackStratumTLS')?.updateValueAndValidity();

      this.savedProfiles = profiles;
      this.profileForm = this.buildProfileForm(profiles[0] ?? null);
      if (profiles.length > 0) {
        this.managedProfile = profiles[0];
      }

      // Restore pool profile selections from the IDs saved in NVS
      const matchById = (id: number | undefined) =>
        (id != null && id >= 0) ? profiles.find(p => p.id === id) ?? null : null;
      this.selectedPrimaryProfile = matchById(info.stratumProfileId);
      this.selectedFallbackProfile = matchById(info.fallbackStratumProfileId);

      const primaryFields = ['stratumURL', 'stratumPort', 'stratumUser', 'stratumPassword', 'stratumTLS', 'stratumCert', 'stratumExtranonceSubscribe', 'stratumSuggestedDifficulty', 'stratumDecodeCoinbase'];
      const fallbackFields = ['fallbackStratumURL', 'fallbackStratumPort', 'fallbackStratumUser', 'fallbackStratumPassword', 'fallbackStratumTLS', 'fallbackStratumCert', 'fallbackStratumExtranonceSubscribe', 'fallbackStratumSuggestedDifficulty', 'fallbackStratumDecodeCoinbase'];
      merge(
        ...primaryFields.map(n => this.form.get(n)!.valueChanges.pipe(map(() => 'primary' as const))),
        ...fallbackFields.map(n => this.form.get(n)!.valueChanges.pipe(map(() => 'fallback' as const))),
      ).pipe(takeUntil(this.destroy$)).subscribe(which => {
        if (which === 'primary' && this.selectedPrimaryProfile) {
          this.selectedPrimaryProfile = null;
        }
        if (which === 'fallback' && this.selectedFallbackProfile) {
          this.selectedFallbackProfile = null;
        }
      });
    });
  }

  ngOnDestroy(): void {
    this.destroy$.next();
    this.destroy$.complete();
  }

  public onManageProfileChange(profile: PoolProfile | null): void {
    this.isNewProfile = false;
    this.managedProfile = profile;
    if (profile) {
      this.profileForm.patchValue({
        name: profile.name ?? '',
        stratumURL: profile.stratumURL ?? '',
        stratumPort: profile.stratumPort ?? 3333,
        stratumUser: profile.stratumUser ?? '',
        stratumPassword: profile.stratumPassword ?? '',
        stratumTLS: profile.stratumTLS ?? 0,
        stratumCert: profile.stratumCert ?? '',
        stratumExtranonceSubscribe: profile.stratumExtranonceSubscribe ?? false,
        stratumSuggestedDifficulty: profile.stratumSuggestedDifficulty ?? 0,
        stratumDecodeCoinbase: profile.stratumDecodeCoinbase ?? 3,
      }, { emitEvent: false });
      this.profileForm.markAsPristine();
      this.profileForm.get('stratumTLS')?.updateValueAndValidity();
    }
  }

  public newProfile(): void {
    this.isNewProfile = true;
    this.managedProfile = null;
    this.showProfilePassword = false;
    const f = this.form?.getRawValue();
    setTimeout(() => {
      this.profileForm.reset({
        name: '',
        stratumURL: f?.stratumURL ?? '',
        stratumPort: f?.stratumPort ?? 3333,
        stratumUser: f?.stratumUser ?? '',
        stratumPassword: 'x',
        stratumTLS: f?.stratumTLS ?? 0,
        stratumCert: f?.stratumCert ?? '',
        stratumExtranonceSubscribe: f?.stratumExtranonceSubscribe ?? false,
        stratumSuggestedDifficulty: f?.stratumSuggestedDifficulty ?? 1000,
        stratumDecodeCoinbase: f?.stratumDecodeCoinbase ?? 3,
      });
    });
  }

  public saveProfileForm(): void {
    if (!this.profileForm?.valid) return;
    const data = this.profileForm.getRawValue();
    if (this.isNewProfile) {
      this.systemService.savePoolProfile(data, this.uri).pipe(takeUntil(this.destroy$)).subscribe({
        next: (result) => {
          this.isNewProfile = false;
          this.systemService.getPoolProfiles(this.uri).pipe(takeUntil(this.destroy$)).subscribe(p => {
            this.savedProfiles = p;
            this.managedProfile = p.find(x => x.id === result?.id) ?? p[p.length - 1] ?? null;
            if (this.managedProfile) {
              this.profileForm.patchValue({
                name: this.managedProfile.name ?? '',
                stratumURL: this.managedProfile.stratumURL ?? '',
                stratumPort: this.managedProfile.stratumPort ?? 3333,
                stratumUser: this.managedProfile.stratumUser ?? '',
                stratumPassword: this.managedProfile.stratumPassword ?? '',
                stratumTLS: this.managedProfile.stratumTLS ?? 0,
                stratumCert: this.managedProfile.stratumCert ?? '',
                stratumExtranonceSubscribe: this.managedProfile.stratumExtranonceSubscribe ?? false,
                stratumSuggestedDifficulty: this.managedProfile.stratumSuggestedDifficulty ?? 0,
                stratumDecodeCoinbase: this.managedProfile.stratumDecodeCoinbase ?? 3,
              }, { emitEvent: false });
              this.profileForm.markAsPristine();
              this.profileForm.get('stratumTLS')?.updateValueAndValidity();
            }
          });
          this.toastr.success('Profile saved');
        },
        error: () => this.toastr.error('Failed to save profile'),
      });
    } else if (this.managedProfile) {
      this.systemService.updatePoolProfile(this.managedProfile.id, data, this.uri).pipe(takeUntil(this.destroy$)).subscribe({
        next: () => {
          this.reloadProfiles(this.managedProfile!.id);
          this.toastr.success('Profile updated');
        },
        error: () => this.toastr.error('Failed to update profile'),
      });
    }
  }

  public onApplyPrimaryProfile(profile: PoolProfile | null): void {
    if (!profile) {
      this.selectedPrimaryProfile = null;
      return;
    }
    this.selectedPrimaryProfile = profile;
    this.form.patchValue({
      stratumURL: profile.stratumURL,
      stratumPort: profile.stratumPort,
      stratumUser: profile.stratumUser,
      stratumPassword: profile.stratumPassword || '*****',
      stratumTLS: profile.stratumTLS,
      stratumCert: profile.stratumCert || '',
      stratumExtranonceSubscribe: profile.stratumExtranonceSubscribe,
      stratumSuggestedDifficulty: profile.stratumSuggestedDifficulty,
      stratumDecodeCoinbase: profile.stratumDecodeCoinbase,
    }, { emitEvent: false });
    this.form.markAsDirty();
  }

  public onApplyFallbackProfile(profile: PoolProfile | null): void {
    if (!profile) {
      this.selectedFallbackProfile = null;
      return;
    }
    this.selectedFallbackProfile = profile;
    this.form.patchValue({
      fallbackStratumURL: profile.stratumURL,
      fallbackStratumPort: profile.stratumPort,
      fallbackStratumUser: profile.stratumUser,
      fallbackStratumPassword: profile.stratumPassword || '*****',
      fallbackStratumTLS: profile.stratumTLS,
      fallbackStratumCert: profile.stratumCert || '',
      fallbackStratumExtranonceSubscribe: profile.stratumExtranonceSubscribe,
      fallbackStratumSuggestedDifficulty: profile.stratumSuggestedDifficulty,
      fallbackStratumDecodeCoinbase: profile.stratumDecodeCoinbase,
    }, { emitEvent: false });
    this.form.markAsDirty();
  }

  private buildProfileForm(profile: PoolProfile | null): FormGroup {
    const form = this.fb.group({
      name: [profile?.name ?? '', [Validators.required, Validators.maxLength(32)]],
      stratumURL: [profile?.stratumURL ?? '', [
        Validators.required,
        Validators.pattern(/^(?!.*stratum\+tcp:\/\/)(?!.*:[1-9]\d{0,4}$).*$/),
      ]],
      stratumPort: [profile?.stratumPort ?? 3333, [
        Validators.required,
        Validators.min(0),
        Validators.max(65535)
      ]],
      stratumUser: [profile?.stratumUser ?? '', [Validators.required]],
      stratumPassword: [profile?.stratumPassword ?? ''],
      stratumTLS: [profile?.stratumTLS ?? 0],
      stratumCert: [profile?.stratumCert ?? ''],
      stratumExtranonceSubscribe: [profile?.stratumExtranonceSubscribe ?? false],
      stratumSuggestedDifficulty: [profile?.stratumSuggestedDifficulty ?? 0, [Validators.required]],
      stratumDecodeCoinbase: [profile?.stratumDecodeCoinbase ?? 3, [Validators.required]],
    });

    const certControl = form.get('stratumCert');
    form.get('stratumTLS')?.valueChanges.subscribe(tls => {
      if (tls === 2) {
        certControl?.setValidators([Validators.required, this.pemCertificateValidator()]);
      } else {
        certControl?.clearValidators();
      }
      certControl?.updateValueAndValidity();
    });
    form.get('stratumTLS')?.updateValueAndValidity();

    return form;
  }

  public saveProfile(): void {
    if (!this.profileForm?.valid) return;
    const data = this.profileForm.getRawValue();
    if (this.isNewProfile) {
      this.systemService.savePoolProfile(data, this.uri).pipe(takeUntil(this.destroy$)).subscribe({
        next: (result) => {
          this.isNewProfile = false;
          this.systemService.getPoolProfiles(this.uri).pipe(takeUntil(this.destroy$)).subscribe(p => {
            this.savedProfiles = p;
            this.managedProfile = p.find(x => x.id === result?.id) ?? p[p.length - 1] ?? null;
            if (this.managedProfile) {
              this.profileForm.patchValue({ name: this.managedProfile.name ?? '' }, { emitEvent: false });
              this.profileForm.markAsPristine();
            }
          });
          this.toastr.success('Profile saved');
        },
        error: () => this.toastr.error('Failed to save profile'),
      });
    } else if (this.managedProfile) {
      this.systemService.updatePoolProfile(this.managedProfile.id, data, this.uri).pipe(takeUntil(this.destroy$)).subscribe({
        next: () => {
          this.reloadProfiles(this.managedProfile!.id);
          this.toastr.success('Profile updated');
        },
        error: () => this.toastr.error('Failed to update profile'),
      });
    }
  }

  public deleteProfile(profile: PoolProfile): void {
    this.systemService.deletePoolProfile(profile.id, this.uri).pipe(takeUntil(this.destroy$)).subscribe({
      next: () => {
        if (this.managedProfile?.id === profile.id) this.managedProfile = null;
        if (this.selectedPrimaryProfile?.id === profile.id) this.selectedPrimaryProfile = null;
        if (this.selectedFallbackProfile?.id === profile.id) this.selectedFallbackProfile = null;
        this.reloadProfiles();
        this.toastr.success('Profile deleted');
      },
      error: () => this.toastr.error('Failed to delete profile'),
    });
  }

  private reloadProfiles(updatedId?: number): void {
    this.systemService.getPoolProfiles(this.uri).pipe(takeUntil(this.destroy$)).subscribe(p => {
      this.savedProfiles = p;
      if (this.managedProfile) {
        this.managedProfile = p.find(x => x.id === this.managedProfile!.id) ?? null;
        if (this.managedProfile) {
          this.profileForm.patchValue({
            name: this.managedProfile.name ?? '',
            stratumURL: this.managedProfile.stratumURL ?? '',
            stratumPort: this.managedProfile.stratumPort ?? 3333,
            stratumUser: this.managedProfile.stratumUser ?? '',
            stratumPassword: this.managedProfile.stratumPassword ?? '',
            stratumTLS: this.managedProfile.stratumTLS ?? 0,
            stratumCert: this.managedProfile.stratumCert ?? '',
            stratumExtranonceSubscribe: this.managedProfile.stratumExtranonceSubscribe ?? false,
            stratumSuggestedDifficulty: this.managedProfile.stratumSuggestedDifficulty ?? 0,
            stratumDecodeCoinbase: this.managedProfile.stratumDecodeCoinbase ?? 3,
          }, { emitEvent: false });
          this.profileForm.markAsPristine();
          this.profileForm.get('stratumTLS')?.updateValueAndValidity();
        }
      }
      if (this.selectedPrimaryProfile) {
        if (updatedId != null && this.selectedPrimaryProfile.id === updatedId) {
          this.selectedPrimaryProfile = null;
        } else {
          this.selectedPrimaryProfile = p.find(x => x.id === this.selectedPrimaryProfile!.id) ?? null;
        }
      }
      if (this.selectedFallbackProfile) {
        if (updatedId != null && this.selectedFallbackProfile.id === updatedId) {
          this.selectedFallbackProfile = null;
        } else {
          this.selectedFallbackProfile = p.find(x => x.id === this.selectedFallbackProfile!.id) ?? null;
        }
      }
    });
  }

  public onProfileUrlChange(): void {
    const urlControl = this.profileForm.get('stratumURL');
    const portControl = this.profileForm.get('stratumPort');
    const tlsControl = this.profileForm.get('stratumTLS');
    if (!urlControl || !portControl || !tlsControl) return;
    let urlValue = urlControl.value?.trim() || '';
    if (!urlValue) return;
    const prefixes = [
      { prefix: 'stratum+tcp://', tlsMode: false },
      { prefix: 'stratum+tls://', tlsMode: true },
      { prefix: 'stratum+ssl://', tlsMode: true }
    ] as const;
    let isTlsMode = 0;
    const matched = prefixes.find(({ prefix }) => urlValue.startsWith(prefix));
    if (matched) {
      urlValue = urlValue.slice(matched.prefix.length);
      isTlsMode = +matched.tlsMode;
    }
    const { cleanUrl, port } = this.extractPort(urlValue);
    if (port !== undefined) portControl.setValue(port);
    urlControl.setValue(cleanUrl);
    tlsControl.setValue(isTlsMode);
  }

  public updateSystem() {
    const form = this.form.getRawValue();

    if (form.stratumPassword === '*****') {
      delete form.stratumPassword;
    }
    if (form.fallbackStratumPassword === '*****') {
      delete form.fallbackStratumPassword;
    }

    // Persist which pool profile (if any) is active
    form.stratumProfileId = this.selectedPrimaryProfile?.id ?? -1;
    form.fallbackStratumProfileId = this.selectedFallbackProfile?.id ?? -1;

    this.systemService.updateSystem(this.uri, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          const successMessage = this.uri ? `Saved pool settings for ${this.uri}` : 'Saved pool settings';
          this.toastr.warning('You must restart this device after saving for changes to take effect.');
          this.toastr.success(successMessage);
          this.savedChanges = true;
        },
        error: (err: HttpErrorResponse) => {
          const errorMessage = this.uri ? `Could not save pool settings for ${this.uri}. ${err.message}` : `Could not save pool settings. ${err.message}`;
          this.toastr.error(errorMessage);
          this.savedChanges = false;
        }
      });
  }

  public restart() {
    this.systemService.restart(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          const successMessage = this.uri ? `Device at ${this.uri} restarted` : 'Device restarted';
          this.toastr.success(successMessage);
        },
        error: (err: HttpErrorResponse) => {
          const errorMessage = this.uri ? `Failed to restart device at ${this.uri}. ${err.message}` : `Failed to restart device. ${err.message}`;
          this.toastr.error(errorMessage);
        }
      });
  }

  private extractPort(url: string): { cleanUrl: string, port?: number } {
    const match = url.match(/:(\d{1,5})$/);
    if (match) {
      const port = parseInt(match[1], 10);
      return { cleanUrl: url.slice(0, match.index), port };
    }
    return { cleanUrl: url };
  }

  public onUrlChange(poolType: PoolType) {
    const urlControl = this.form.get(`${poolType}URL`);
    const portControl = this.form.get(`${poolType}Port`);
    const tlsControl = this.form.get(`${poolType}TLS`);
    if (!urlControl || !portControl || !tlsControl) return;

    let urlValue = urlControl.value.trim() || '';
    if (!urlValue) return;

    const prefixes = [
      { prefix: 'stratum+tcp://', tlsMode: false },
      { prefix: 'stratum+tls://', tlsMode: true },
      { prefix: 'stratum+ssl://', tlsMode: true }
    ] as const;

    let isTlsMode = 0;
    const matched = prefixes.find(({ prefix }) => urlValue.startsWith(prefix));
    if (matched) {
      urlValue = urlValue.slice(matched.prefix.length);
      isTlsMode = +matched.tlsMode;
    }

    const { cleanUrl, port } = this.extractPort(urlValue);
    if (port !== undefined) portControl.setValue(port);
    urlControl.setValue(cleanUrl);
    tlsControl.setValue(isTlsMode);
  }

  onCertFileSelected(event: Event, formControlName: string, targetForm?: FormGroup): void {
    const form = targetForm ?? this.form;
    const fileInput = event.target as HTMLInputElement;
    if (fileInput.files && fileInput.files.length > 0) {
      const file = fileInput.files[0];
      const reader = new FileReader();
      reader.onload = () => {
        const fileContent = reader.result as string;
        form.get(formControlName)?.setValue(fileContent);
        form.get(formControlName)?.markAsDirty();
        fileInput.value = '';
      };
      reader.onerror = () => {
        this.toastr.error('Failed to read certificate file');
        fileInput.value = '';
      };
      reader.readAsText(file);
    }
  }

  private pemCertificateValidator(): ValidatorFn {
    return (control: AbstractControl): ValidationErrors | null => {
      const value = control.value?.trim();
      if (!value) return null;
      const pemChainRegex =
        /^(?:-----BEGIN CERTIFICATE-----[\s\S]*?-----END CERTIFICATE-----\s*)+$/;
      return pemChainRegex.test(value) ? null : { invalidCertificate: true };
    };
  }

  get profileTlsValue(): number {
    return this.profileForm?.get('stratumTLS')?.value ?? 0;
  }
  set profileTlsValue(v: number) {
    this.profileForm?.get('stratumTLS')?.setValue(v);
    this.profileForm?.get('stratumTLS')?.markAsDirty();
  }

  trackByFn(index: number, option: ITlsOption): number {
    return option.value;
  }

  isUsingDefaultAddress(pool: PoolType): boolean {
    const userValue = this.form?.get(pool + 'User')?.value || '';
    return userValue.includes(this.DEFAULT_BITCOIN_ADDRESS);
  }

  isAnyPoolUsingDefaultAddress(): boolean {
    return this.pools.some(pool => this.isUsingDefaultAddress(pool));
  }

  exportAllProfiles(): void {
    if (!this.savedProfiles.length) return;
    const data = this.savedProfiles.map(({ id, ...p }) => p);
    const json = JSON.stringify(data, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'pool-profiles.json';
    a.click();
    URL.revokeObjectURL(url);
  }

  importProfiles(event: Event): void {
    const input = event.target as HTMLInputElement;
    const file = input.files?.[0];
    if (!file) return;
    input.value = '';
    const reader = new FileReader();
    reader.onload = () => {
      try {
        const parsed = JSON.parse(reader.result as string);
        const profiles: any[] = Array.isArray(parsed) ? parsed : [parsed];
        if (profiles.length === 0) { this.toastr.error('No profiles found in file'); return; }
        if (profiles.some(p => typeof p.name !== 'string' || typeof p.stratumURL !== 'string')) {
          this.toastr.error('Invalid profile file'); return;
        }
        const available = 8 - this.savedProfiles.length;
        if (profiles.length > available) {
          this.toastr.error(`Cannot import ${profiles.length} profiles — only ${available} slot${available === 1 ? '' : 's'} remaining`);
          return;
        }
        const saves = profiles.map(({ id, ...p }) => this.systemService.savePoolProfile(p, this.uri));
        forkJoin(saves).pipe(takeUntil(this.destroy$)).subscribe({
          next: () => {
            this.reloadProfiles();
            this.toastr.success(`${profiles.length} profile${profiles.length > 1 ? 's' : ''} imported`);
          },
          error: () => this.toastr.error('Failed to import profiles'),
        });
      } catch {
        this.toastr.error('Invalid profile file');
      }
    };
    reader.readAsText(file);
  }
}
