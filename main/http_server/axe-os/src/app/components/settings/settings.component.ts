import { Component, ViewChild, AfterViewInit, OnDestroy, NgZone } from '@angular/core';
import { FormBuilder, FormControl, FormGroup, Validators } from '@angular/forms';
import { Observable, Subject, takeUntil, forkJoin, pairwise } from 'rxjs';
import { EditComponent } from '../edit/edit.component';
import { SystemApiService } from 'src/app/services/system.service';
import { ToastrService } from 'ngx-toastr';

@Component({
  selector: 'app-settings',
  templateUrl: './settings.component.html',
  styles: [
    `
      .profile-action-toolbar .profile-action-button {
        color: #ffffff;
        background: transparent;
        border: none;
      }
      .profile-action-toolbar .profile-action-button:hover:not(.p-button-disabled) {
        background-color: rgba(255, 255, 255, 0.14);
      }
      .profile-action-toolbar .profile-action-button .pi {
        color: #ffffff;
      }
      .profile-action-toolbar .profile-action-button.p-button-disabled {
        color: rgba(255, 255, 255, 0.45);
        background: transparent !important;
      }
      .profile-save-dialog .p-dialog-content {
        overflow-x: hidden !important;
      }
      .profile-save-dialog .p-grid {
        margin-left: 0;
        margin-right: 0;
      }
      .profile-save-dialog .profile-save-dialog-body {
        overflow-x: hidden;
      }
      .profile-dirty-indicator {
        font-size: 0.55rem;
        color: var(--orange-400);
        margin-left: 0.25rem;
        vertical-align: super;
      }
      .settings-tabview-wrapper {
        position: relative;
      }
      .profile-tab-actions {
        position: absolute;
        top: 5px;
        right: 0;
        z-index: 10;
        display: flex;
        align-items: center;
        gap: 4px;
      }
      :host ::ng-deep .p-tabview .p-tabview-nav {
        border: none;
        background: transparent;
      }
      :host ::ng-deep .p-tabview .p-tabview-nav li .p-tabview-nav-link {
        border: none;
        background: transparent;
        opacity: 0.4;
        transition: opacity 0.15s;
      }
      :host ::ng-deep .p-tabview .p-tabview-nav li.p-highlight .p-tabview-nav-link {
        opacity: 1;
        border: none;
        background: transparent;
      }
      :host ::ng-deep .p-tabview .p-tabview-nav li:not(.p-highlight) .p-tabview-nav-link:hover {
        opacity: 0.7;
      }
      :host ::ng-deep .p-tabview .p-tabview-panels {
        border: none;
        background: transparent;
        padding: 1rem 0 0 0;
      }
    `
  ]
})
export class SettingsComponent implements AfterViewInit, OnDestroy {
  form$!: Observable<FormGroup | null>;

  @ViewChild(EditComponent) editComponent!: EditComponent;

  public savedProfiles: any[] = [];
  public profileOptions: any[] = [];
  public selectedProfile: any = null;   // Tab 1: which profile is applied to live settings
  public managedProfile: any = null;    // Tab 2: which profile is open for editing

  public isNewProfile = false;
  public activeTabIndex = 0;
  public profileForm: FormGroup | null = null;

  public fanModeOptions = [
    { label: 'Off', value: 0 },
    { label: 'ASIC', value: 1 },
    { label: 'ASIC + VRR', value: 2 },
  ];
  public fanCurveOptions = [
    { label: 'Default', value: 0 },
    { label: 'Performance', value: 1 },
    { label: 'Aggressive', value: 2 },
  ];
  public displays = ['NONE', 'SSD1306 (128x32)', 'SSD1309 (128x64)', 'SH1107 (64x128)', 'SH1107 (128x128)'];
  public rotations = [0, 90, 180, 270];

  private readonly DISPLAY_TIMEOUT_STEPS = [0, 1, 2, 5, 15, 30, 60, 120, 240, 480, -1];
  private readonly STATS_FREQUENCY_STEPS = [0, 30, 60, 120, 360, 840, 1680, 3600];

  public profileDisplayTimeoutControl = new FormControl(0);
  public profileStatsFrequencyControl = new FormControl(0);

  get profileDisplayTimeoutMaxSteps(): number { return this.DISPLAY_TIMEOUT_STEPS.length - 1; }
  get profileStatsFrequencyMaxSteps(): number { return this.STATS_FREQUENCY_STEPS.length - 1; }
  public showImportConfirm: boolean = false;
  public importConfirmMessage: string = '';
  private pendingImport: { profiles: any[], replace: boolean } | null = null;

  private destroy$ = new Subject<void>();

  public formatDangerZone(value: number): string {
    return value === 1 ? 'Enabled' : 'Disabled';
  }

  public formatDisplayLabel(value: any): string {
    if (typeof value === 'string') return value;
    return this.editComponent?.displays?.[value] ?? 'Unknown';
  }

  public formatOcStep(step: number): string {
    return this.editComponent?.ocStepLabel(step) ?? String(step);
  }

  public formatDisplayTimeout(value: number): string {
    if (value === 0) {
      return 'Always on';
    }
    if (value === -1) {
      return 'Never';
    }
    return `${value} min`;
  }

  public get currentProfileForm(): any {
    return this.editComponent?.form?.getRawValue() ?? null;
  }

  constructor(
    private fb: FormBuilder,
    private systemService: SystemApiService,
    private toastr: ToastrService,
    private ngZone: NgZone,
  ) {
    this.profileDisplayTimeoutControl.valueChanges.pipe(pairwise()).subscribe(([prev, next]) => {
      if (prev === next) return;
      if (this.profileForm) {
        this.profileForm.patchValue({ displayTimeout: this.DISPLAY_TIMEOUT_STEPS[next!] });
        this.profileForm.controls['displayTimeout'].markAsDirty();
      }
    });
    this.profileStatsFrequencyControl.valueChanges.pipe(pairwise()).subscribe(([prev, next]) => {
      if (prev === next) return;
      if (this.profileForm) {
        this.profileForm.patchValue({ statsFrequency: this.STATS_FREQUENCY_STEPS[next!] });
        this.profileForm.controls['statsFrequency'].markAsDirty();
      }
    });
  }

  ngAfterViewInit() {
    this.form$ = this.editComponent.form$;
    this.form$.pipe(takeUntil(this.destroy$)).subscribe(form => {
      if (form) {
        this.rebuildProfileOptions();
        form.valueChanges.pipe(takeUntil(this.destroy$)).subscribe(() => {
          this.selectedProfile = null;
        });
        this.loadProfiles();
      }
    });
  }

  ngOnDestroy() {
    this.destroy$.next();
    this.destroy$.complete();
  }

  private rebuildProfileOptions(): void {
    this.profileOptions = [...this.savedProfiles];
    if (this.selectedProfile) {
      this.selectedProfile = this.savedProfiles.find(p => p.id === this.selectedProfile.id) ?? null;
    }
    if (this.managedProfile) {
      this.managedProfile = this.savedProfiles.find(p => p.id === this.managedProfile.id) ?? null;
    }
  }

  loadProfiles(): void {
    forkJoin({
      profiles: this.systemService.getProfiles(),
      info: this.systemService.getInfo(),
    }).pipe(takeUntil(this.destroy$)).subscribe(({ profiles, info }) => {
      this.savedProfiles = profiles;
      this.rebuildProfileOptions();
      if (!this.selectedProfile) {
        const savedId = (info as any).selectedProfileId;
        if (savedId != null && savedId >= 0) {
          const found = this.savedProfiles.find(p => p.id === savedId);
          if (found) {
            this.selectedProfile = found;
            this.editComponent.loadProfile(found);
          }
        }
      }
    });
  }

  private reloadProfiles(updatedId?: number): void {
    this.systemService.getProfiles().pipe(takeUntil(this.destroy$)).subscribe(profiles => {
      this.savedProfiles = profiles;
      this.rebuildProfileOptions();
      const found = this.savedProfiles.find(p => p.id === updatedId!);
      if (found) {
        this.managedProfile = found;
        this.profileForm = this.buildProfileForm(found);
        if (updatedId != null && this.selectedProfile?.id === updatedId) {
          this.selectedProfile = null;
        }
      }
    });
  }

  // Tab 1: apply a profile to live settings
  onApplyProfile(profile: any): void {
    if (!profile) return;
    this.selectedProfile = profile;
    this.editComponent.loadProfile(profile);
    this.editComponent.form.markAsDirty();
  }

  // Tab 2: open a profile for editing
  onManageProfileChange(profile: any): void {
    if (!profile) return;
    this.managedProfile = profile;
    this.isNewProfile = false;
    this.profileForm = this.buildProfileForm(profile);
  }

  onSectionSaved(): void {}

  onReverted(): void {}

  newProfile(): void {
    if (this.savedProfiles.length >= 4) {
      this.toastr.error('Maximum of 4 profiles reached');
      return;
    }
    this.isNewProfile = true;
    this.managedProfile = null;
    const f = this.editComponent?.form?.getRawValue() ?? {};
    // edit form uses 'minfanspeed' (lowercase); buildProfileForm expects 'minFanSpeed'
    if (f.minfanspeed != null && f.minFanSpeed == null) f.minFanSpeed = f.minfanspeed;
    this.profileForm = this.buildProfileForm({ ...f, name: '' });
  }

  saveProfileForm(): void {
    if (!this.profileForm?.valid) return;
    const data = this.profileForm.getRawValue();
    const { name, ...rest } = data;
    if (!name?.trim()) { this.toastr.error('Profile name is required'); return; }
    if (this.isNewProfile) {
      const duplicate = this.savedProfiles.some(p => p.name.toLowerCase() === name.trim().toLowerCase());
      if (duplicate) { this.toastr.error('A profile with that name already exists'); return; }
      this.systemService.saveProfile({ name: name.trim(), ...rest }).pipe(takeUntil(this.destroy$)).subscribe({
        next: (res) => { this.isNewProfile = false; this.reloadProfiles(res.id); this.toastr.success('Profile saved'); },
        error: () => this.toastr.error('Failed to save profile'),
      });
    } else if (this.managedProfile) {
      this.systemService.updateProfile(this.managedProfile.id, { name: name.trim(), ...rest }).pipe(takeUntil(this.destroy$)).subscribe({
        next: () => { this.reloadProfiles(this.managedProfile.id); this.toastr.success('Profile saved'); },
        error: () => this.toastr.error('Failed to save profile'),
      });
    }
  }

  private buildProfileForm(profile: any | null): FormGroup {
    const dtStep = this.DISPLAY_TIMEOUT_STEPS.indexOf(profile?.displayTimeout ?? 0);
    this.profileDisplayTimeoutControl.setValue(dtStep >= 0 ? dtStep : 0, { emitEvent: false });

    const sfStep = this.STATS_FREQUENCY_STEPS.indexOf(profile?.statsFrequency ?? 0);
    this.profileStatsFrequencyControl.setValue(sfStep >= 0 ? sfStep : 0, { emitEvent: false });

    return this.fb.group({
      name: [profile?.name ?? '', [Validators.required, Validators.maxLength(32)]],
      frequency: [profile?.frequency ?? null, [Validators.required]],
      coreVoltage: [profile?.coreVoltage ?? null, [Validators.required]],
      overclockEnabled: [profile?.overclockEnabled ?? 0],
      autofanspeed: [profile?.autofanspeed ?? 1, [Validators.required]],
      manualFanSpeed: [profile?.manualFanSpeed ?? 50, [Validators.required]],
      minFanSpeed: [profile?.minFanSpeed ?? 10, [Validators.required]],
      fanCurve: [profile?.fanCurve ?? 0, [Validators.required]],
      temptarget: [profile?.temptarget ?? 60, [Validators.required]],
      vrrtarget: [profile?.vrrtarget ?? 70, [Validators.required]],
      dangerzone: [profile?.dangerzone ?? 0],
      ocFaultStep: [profile?.ocFaultStep ?? 0],
      overheat_mode: [profile?.overheat_mode ?? 0, [Validators.required]],
      display: [profile?.display ?? 'NONE', [Validators.required]],
      rotation: [profile?.rotation ?? 0, [Validators.required]],
      invertscreen: [profile?.invertscreen ?? false],
      displayTimeout: [profile?.displayTimeout ?? 0, [Validators.required]],
      heliosStatsEnabled: [profile?.heliosStatsEnabled ?? 1],
      statsFrequency: [profile?.statsFrequency ?? 0, [Validators.required]],
    });
  }

  deleteProfile(p: any): void {
    this.systemService.deleteProfile(p.id).pipe(takeUntil(this.destroy$)).subscribe({
      next: () => {
        if (this.managedProfile?.id === p.id) {
          this.managedProfile = null;
          this.profileForm = null;
        }
        if (this.selectedProfile?.id === p.id) {
          this.selectedProfile = null;
        }
        this.isNewProfile = false;
        this.loadProfiles();
        this.toastr.success('Profile deleted');
      },
      error: () => this.toastr.error('Failed to delete profile'),
    });
  }



  get profileDropdownFrequency(): {name: string; value: number}[] {
    return this.editComponent?.dropdownFrequency ?? [];
  }

  get profileDropdownVoltage(): {name: string; value: number}[] {
    return this.editComponent?.dropdownVoltage ?? [];
  }

  exportAllProfiles(): void {
    if (!this.savedProfiles.length) return;
    const data = this.savedProfiles.map(({ id, ...p }) => p);
    const json = JSON.stringify(data, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'profiles-all.json';
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
      this.ngZone.run(() => {
      try {
        const parsed = JSON.parse(reader.result as string);
        if (Array.isArray(parsed)) {
          // Bulk import
          if (parsed.length === 0) { this.toastr.error('No profiles found in file'); return; }
          if (parsed.length > 4) { this.toastr.error(`File contains ${parsed.length} profiles — maximum is 4`); return; }
          if (parsed.some(p => typeof p.name !== 'string' || typeof p.frequency !== 'number')) {
            this.toastr.error('Invalid profile file'); return;
          }
          this.pendingImport = { profiles: parsed, replace: true };
          this.importConfirmMessage = `Replace all current profiles with ${parsed.length} profile${parsed.length > 1 ? 's' : ''} from file?`;
          this.showImportConfirm = true;
        } else {
          // Single profile
          if (!parsed || typeof parsed.name !== 'string' || typeof parsed.frequency !== 'number') {
            this.toastr.error('Invalid profile file'); return;
          }
          const name = parsed.name.trim();
          if (!name) { this.toastr.error('Invalid profile file'); return; }
          const duplicate = this.savedProfiles.find(p => p.name.toLowerCase() === name.toLowerCase());
          if (!duplicate && this.savedProfiles.length >= 4) {
            this.toastr.error('Maximum of 4 profiles reached'); return;
          }
          this.pendingImport = { profiles: [parsed], replace: false };
          this.importConfirmMessage = duplicate
            ? `Overwrite "${name}" profile?`
            : `Import "${name}" profile?`;
          this.showImportConfirm = true;
        }
      } catch {
        this.toastr.error('Invalid profile file');
      }
      }); // ngZone.run
    };
    reader.readAsText(file);
  }

  confirmImport(): void {
    this.showImportConfirm = false;
    if (!this.pendingImport) return;
    const { profiles, replace } = this.pendingImport;
    this.pendingImport = null;

    if (replace) {
      // Delete all existing, then save each in sequence
      const deletions = this.savedProfiles.map(p =>
        this.systemService.deleteProfile(p.id)
      );
      if (deletions.length === 0) {
        this.saveImportedProfiles(profiles);
        return;
      }
      forkJoin(deletions).pipe(takeUntil(this.destroy$)).subscribe({
        next: () => this.saveImportedProfiles(profiles),
        error: () => this.toastr.error('Failed to replace profiles'),
      });
    } else {
      const profile = profiles[0];
      const { id, ...data } = profile;
      const duplicate = this.savedProfiles.find(p => p.name.toLowerCase() === data.name.toLowerCase());
      if (duplicate) {
        this.systemService.updateProfile(duplicate.id, data).pipe(takeUntil(this.destroy$)).subscribe({
          next: () => { this.reloadProfiles(duplicate.id); this.toastr.success('Profile imported'); },
          error: () => this.toastr.error('Failed to import profile'),
        });
      } else {
        this.systemService.saveProfile(data).pipe(takeUntil(this.destroy$)).subscribe({
          next: (res) => { this.reloadProfiles(res.id); this.toastr.success('Profile imported'); },
          error: () => this.toastr.error('Failed to import profile'),
        });
      }
    }
  }

  private saveImportedProfiles(profiles: any[]): void {
    const saves = profiles.map(({ id, ...p }) => this.systemService.saveProfile(p));
    forkJoin(saves).pipe(takeUntil(this.destroy$)).subscribe({
      next: (results) => {
        const lastId = results[results.length - 1].id;
        this.reloadProfiles(lastId);
        this.toastr.success(`${profiles.length} profile${profiles.length > 1 ? 's' : ''} imported`);
      },
      error: () => this.toastr.error('Failed to import profiles'),
    });
  }
}
