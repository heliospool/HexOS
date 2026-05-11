import { HttpErrorResponse } from '@angular/common/http';
import { Component, EventEmitter, Input, OnInit, OnDestroy, OnChanges, Output, SimpleChanges } from '@angular/core';
import { FormBuilder, FormGroup, FormControl, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { forkJoin, startWith, Subject, takeUntil, pairwise, BehaviorSubject, Observable } from 'rxjs';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemApiService } from 'src/app/services/system.service';
import { SystemInfo } from 'src/app/generated';

type Dropdown = {
  name: string;
  value: number;
}[]

const DISPLAY_TIMEOUT_STEPS = [0, 1, 2, 5, 15, 30, 60, 60 * 2, 60 * 4, 60* 8, -1];
const STATS_FREQUENCY_STEPS = [0, 30, 60, 60 * 2, 60 * 6, 60 * 14, 60 * 28, 60 * 60];

@Component({
  selector: 'app-edit',
  templateUrl: './edit.component.html'
})

export class EditComponent implements OnInit, OnDestroy, OnChanges {
  private formSubject = new BehaviorSubject<FormGroup | null>(null);
  public form$: Observable<FormGroup | null> = this.formSubject.asObservable();

  public form!: FormGroup;
  private savedValues: Record<string, any> = {};
  public restartRequired = false;

  public firmwareUpdateProgress: number | null = null;
  public websiteUpdateProgress: number | null = null;

  public savedChanges: boolean = false;
  public settingsUnlocked: boolean = false;

  @Input() uri = '';
  @Input() extraFields: Record<string, any> = {};
  @Output() sectionSaved = new EventEmitter<void>();
  @Output() reverted = new EventEmitter<void>();

  // Store frequency and voltage options from API
  public defaultFrequency: number = 0;
  public frequencyOptions: number[] = [];
  public defaultVoltage: number = 0;
  public voltageOptions: number[] = [];

  private destroy$ = new Subject<void>();



  public displays = ["NONE", "SSD1306 (128x32)", "SSD1309 (128x64)", "SH1107 (64x128)", "SH1107 (128x128)"];
  public rotations = [0, 90, 180, 270];
  public fanModeOptions = [
    { label: 'Off', value: 0 },
    { label: 'ASIC', value: 1 },
    { label: 'ASIC + VRR', value: 2 },
  ];

  public dangerZoneEnabled = false;
  public dangerZoneConfirmVisible = false;
  public dangerZoneAcknowledged = false;
  public deviceInfo: SystemInfo | null = null;

  toggleDangerZone(enabled: boolean): void {
    if (!enabled) {
      this.dangerZoneEnabled = false;
      this.dangerZoneAcknowledged = false;
      if (this.form) {
        this.form.patchValue({ dangerzone: 0, ocFaultStep: 0 });
        this.form.controls['dangerzone'].markAsDirty();
        this.form.controls['ocFaultStep'].markAsDirty();
      }
      return;
    }
    this.dangerZoneConfirmVisible = true;
  }

  onDangerZoneConfirm(): void {
    if (this.dangerZoneAcknowledged) {
      this.dangerZoneConfirmVisible = false;
      this.dangerZoneEnabled = true;
      if (this.form) {
        this.form.patchValue({ dangerzone: 1 });
        this.form.controls['dangerzone'].markAsDirty();
      }
    }
  }

  onDangerZoneCancel(): void {
    this.dangerZoneConfirmVisible = false;
    this.dangerZoneAcknowledged = false;
    this.dangerZoneEnabled = false;
  }

  public loadProfile(p: any): void {
    const patch: any = {};
    if (p.frequency != null)        patch['frequency']        = p.frequency;
    if (p.coreVoltage != null)      patch['coreVoltage']      = p.coreVoltage;
    if (p.autofanspeed != null)     patch['autofanspeed']     = p.autofanspeed;
    if (p.manualFanSpeed != null)   patch['manualFanSpeed']   = p.manualFanSpeed;
    if (p.minFanSpeed != null)      patch['minfanspeed']      = p.minFanSpeed;
    if (p.fanCurve != null)         patch['fanCurve']         = p.fanCurve;
    if (p.temptarget != null)       patch['temptarget']       = p.temptarget;
    if (p.vrrtarget != null)        patch['vrrtarget']        = p.vrrtarget;
    if (p.dangerzone != null) {
      patch['dangerzone'] = p.dangerzone;
      this.dangerZoneEnabled = p.dangerzone === 1;
    }
    if (p.ocFaultStep != null)      patch['ocFaultStep']      = p.ocFaultStep;
    if (p.overheat_mode != null)    patch['overheat_mode']    = p.overheat_mode;
    if (p.display != null)          patch['display']          = p.display;
    if (p.rotation != null)         patch['rotation']         = p.rotation;
    if (p.invertscreen != null)     patch['invertscreen']     = p.invertscreen;
    if (p.displayTimeout != null)   patch['displayTimeout']   = p.displayTimeout;
    if (p.heliosStatsEnabled != null) patch['heliosStatsEnabled'] = p.heliosStatsEnabled;
    if (p.statsFrequency != null)   patch['statsFrequency']   = p.statsFrequency;
    if (p.overclockEnabled != null) {
      patch['overclockEnabled'] = p.overclockEnabled;
      this.settingsUnlocked = p.overclockEnabled === 1;
    }
    this.form.patchValue(patch, { emitEvent: false });
    if (p.displayTimeout != null) {
      const dtIdx = DISPLAY_TIMEOUT_STEPS.findIndex(x => x === p.displayTimeout);
      if (dtIdx >= 0) this.displayTimeoutControl.setValue(dtIdx, { emitEvent: false });
    }
    if (p.statsFrequency != null) {
      const sfIdx = STATS_FREQUENCY_STEPS.findIndex(x => x === p.statsFrequency);
      if (sfIdx >= 0) this.statsFrequencyControl.setValue(sfIdx, { emitEvent: false });
    }
    this.form.markAsPristine();
    // Do NOT update savedValues here — savedValues tracks device state, not profile state
  }

  public displayTimeoutControl: FormControl;
  public statsFrequencyControl: FormControl;

  constructor(
    private fb: FormBuilder,
    private systemService: SystemApiService,
    private toastr: ToastrService,
    private loadingService: LoadingService,
  ) {
    this.displayTimeoutControl = new FormControl();
    this.displayTimeoutControl.valueChanges.pipe(pairwise()).subscribe(([prev, next]) => {
      if (prev === next) {
        return;
      }

      this.form.patchValue({ displayTimeout: DISPLAY_TIMEOUT_STEPS[next] });
      this.form.controls['displayTimeout'].markAsDirty();
    });

    this.statsFrequencyControl = new FormControl();
    this.statsFrequencyControl.valueChanges.pipe(pairwise()).subscribe(([prev, next]) => {
      if (prev === next) {
        return;
      }

      this.form.patchValue({ statsFrequency: STATS_FREQUENCY_STEPS[next] });
      this.form.controls['statsFrequency'].markAsDirty();
    });
  }


  ngOnInit(): void {
    this.loadDeviceSettings();
  }

  ngOnChanges(changes: SimpleChanges): void {
    // When URI changes, reload the device settings
    if (changes['uri'] && changes['uri'].currentValue && !changes['uri'].firstChange) {
      this.loadDeviceSettings();
    }
  }

  private loadDeviceSettings(): void {
    const deviceUri = this.uri || '';


    // Fetch both system info and ASIC settings in parallel
    forkJoin({
      info: this.systemService.getInfo(deviceUri),
      asic: this.systemService.getAsicSettings(deviceUri)
    })
    .pipe(
      this.loadingService.lockUIUntilComplete(),
      takeUntil(this.destroy$)
    )
    .subscribe(({ info, asic }) => {
        this.deviceInfo = info;
      // Store the frequency and voltage options from the API
      this.defaultFrequency = asic.defaultFrequency;
      this.frequencyOptions = asic.frequencyOptions;
      this.defaultVoltage = asic.defaultVoltage;
      this.voltageOptions = asic.voltageOptions;

      // Check if overclock is enabled in NVS
      if (info.overclockEnabled === 1) {
        this.settingsUnlocked = true;
        console.log(
          '🎉 Overclock mode is enabled from NVS settings!\n' +
          '⚡ Custom frequency and voltage values are available.'
        );
      }

        this.form = this.fb.group({
          display: [info.display, [Validators.required]],
          rotation: [info.rotation, [Validators.required]],
          invertscreen: [info.invertscreen == 1],
          displayTimeout: [info.displayTimeout, [
            Validators.required,
            Validators.min(-1),
            Validators.max(this.displayTimeoutMaxValue)
          ]],
          coreVoltage: [info.coreVoltage, [Validators.required]],
          frequency: [info.frequency, [Validators.required]],
          autofanspeed: [info.autofanspeed ?? 1, [Validators.required]],
          fanCurve: [info.fanCurve ?? 0, [Validators.required]],
          minfanspeed: [info.minFanSpeed, [Validators.required]],
          manualFanSpeed: [info.manualFanSpeed, [Validators.required]],
          temptarget: [info.temptarget, [Validators.required]],
          vrrtarget: [info.vrrtarget > 0 ? info.vrrtarget : 70, [Validators.required]],
          ocFaultStep: [info.ocFaultStep ?? 0],
          dangerzone: [info.dangerzone ?? 0],
          overheat_mode: [info.overheat_mode, [Validators.required]],
          statsFrequency: [info.statsFrequency, [
            Validators.required,
            Validators.min(0),
            Validators.max(this.statsFrequencyMaxValue)
          ]],
          heliosStatsEnabled: [info.heliosStatsEnabled ?? 1],
          overclockEnabled: [info.overclockEnabled ?? 0]
        });

        this.formSubject.next(this.form);

      this.form.controls['autofanspeed'].valueChanges.pipe(
        startWith(this.form.controls['autofanspeed'].value),
        takeUntil(this.destroy$)
      ).subscribe((mode: number) => {
        if (mode > 0) {
          this.form.controls['manualFanSpeed'].disable();
          this.form.controls['temptarget'].enable();
        } else {
          this.form.controls['manualFanSpeed'].enable();
          this.form.controls['temptarget'].disable();
        }
        if (mode !== 2) {
          this.form.patchValue({ vrrtarget: 0 });
        } else if (!this.form.controls['vrrtarget'].value) {
          this.form.patchValue({ vrrtarget: 70 });
        }
      });

      // Add custom value to predefined steps
      if (DISPLAY_TIMEOUT_STEPS.filter(x => x === info.displayTimeout).length === 0) {
        DISPLAY_TIMEOUT_STEPS.push(info.displayTimeout);
        DISPLAY_TIMEOUT_STEPS.sort((a, b) => a - b);
        DISPLAY_TIMEOUT_STEPS.push(DISPLAY_TIMEOUT_STEPS.shift() as number);
      }

      this.displayTimeoutControl.setValue(
        DISPLAY_TIMEOUT_STEPS.findIndex(x => x === info.displayTimeout)
      );

      // Add custom value to predefined steps
      if (STATS_FREQUENCY_STEPS.filter(x => x === info.statsFrequency).length === 0) {
        STATS_FREQUENCY_STEPS.push(info.statsFrequency);
        STATS_FREQUENCY_STEPS.sort((a, b) => a - b);
      }

      this.statsFrequencyControl.setValue(
        STATS_FREQUENCY_STEPS.findIndex(x => x === info.statsFrequency)
      );

      // Capture after all synchronous subscriptions (e.g. autofanspeed startWith) have settled
      Promise.resolve().then(() => {
        this.savedValues = this.form.getRawValue();
        this.dangerZoneEnabled = this.savedValues['dangerzone'] === 1;
      });
    });
  }

  ngOnDestroy(): void {
    this.destroy$.next();
    this.destroy$.complete();
  }

  public ocStepLabel(step: number): string {
    if (!this.deviceInfo) return '';
    if (step === 0) return `Default (${this.deviceInfo.ocFaultDefault}A)`;
    const pct = step * 5;
    const amps = parseFloat((this.deviceInfo.ocFaultDefault * (1 + step * 0.05)).toFixed(2));
    return `+${pct}% (${amps}A)`;
  }

  public updateSystem() {
    const form = this.form.getRawValue();

    // ocFaultStep is already a step index (0-4) — sent directly to API

    if (form.stratumPassword === '*****') {
      delete form.stratumPassword;
    }

    const deviceUri = this.uri || '';
    this.systemService.updateSystem(deviceUri, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          const successMessage = this.uri ? `Saved settings for ${this.uri}` : 'Saved settings';
          if (this.isRestartRequired) {
            this.toastr.warning('You must restart this device after saving for changes to take effect.');
          }
          this.toastr.success(successMessage);
          this.savedChanges = true;
        },
        error: (err: HttpErrorResponse) => {
          const errorMessage = this.uri ? `Could not save settings for ${this.uri}. ${err.message}` : `Could not save settings. ${err.message}`;
          this.toastr.error(errorMessage);
          this.savedChanges = false;
        }
      });
  }

  disableOverheatMode() {
    this.form.patchValue({ overheat_mode: 0 });
    this.updateSystem();
  }

  toggleOverclockMode(enable: boolean) {
    this.settingsUnlocked = enable;
    if (this.form) {
      this.form.patchValue({ overclockEnabled: enable ? 1 : 0 });
      this.form.controls['overclockEnabled'].markAsDirty();
    }
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

  get dropdownFrequency(): Dropdown {
    return this.buildDropdown('frequency', this.frequencyOptions, this.defaultFrequency);
  }

  get dropdownVoltage(): Dropdown {
    return this.buildDropdown('coreVoltage', this.voltageOptions, this.defaultVoltage);
  }

  get displayTimeoutMaxSteps(): number {
    return DISPLAY_TIMEOUT_STEPS.length - 1;
  }

  get displayTimeoutMaxValue(): number {
    return DISPLAY_TIMEOUT_STEPS[this.displayTimeoutMaxSteps - 1];
  }

  get statsFrequencyMaxSteps(): number {
    return STATS_FREQUENCY_STEPS.length - 1;
  }

  get statsFrequencyMaxValue(): number {
    return STATS_FREQUENCY_STEPS[this.statsFrequencyMaxSteps];
  }

  buildDropdown(formField: string, apiOptions: number[], defaultValue: number): Dropdown {
    if (!apiOptions.length) {
      return [];
    }

    // Convert options from API to dropdown format
    const options = apiOptions.map(option => {
      return {
        name: defaultValue === option ? `${option} (Default)` : `${option}`,
        value: option
      };
    });

    // Get current field value from form
    const currentValue = this.form?.get(formField)?.value;

    // If current field value exists and isn't in the options
    if (currentValue && !options.some(opt => opt.value === currentValue)) {
      options.push({
        name: `${currentValue} (Custom)`,
        value: currentValue
      });
      // Sort options by value
      options.sort((a, b) => a.value - b.value);
    }

    return options;
  }

  get noRestartFields(): string[] {
    return [
      'displayTimeout',
      'coreVoltage',
      'frequency',
      'autofanspeed',
      'fanCurve',
      'minfanspeed',
      'manualFanSpeed',
      'temptarget',
      'vrrtarget',
      'overheat_mode',
      'dangerzone',
      'ocFaultStep',
      'statsFrequency',
      'heliosStatsEnabled',
      'overclockEnabled'
    ];
  }

  get isRestartRequired(): boolean {
    return !! Object.entries(this.form.controls)
      .filter(([field, control]) => control.dirty && !this.noRestartFields.includes(field)).length
  }

  public isSectionDirty(fields: string[]): boolean {
    if (!this.form) return false;
    const raw = this.form.getRawValue();
    return fields.some(f => {
      if (this.form.controls[f]?.disabled) return false;
      return raw[f] !== this.savedValues[f];
    });
  }

  public revertToSaved(): void {
    if (!this.form) return;
    this.form.patchValue(this.savedValues, { emitEvent: true });
    this.form.markAsPristine();
    this.dangerZoneEnabled = this.savedValues['dangerzone'] === 1;
    this.reverted.emit();
  }

  public restartDevice(): void {
    this.systemService.restart(this.uri || '').subscribe();
  }

  public isAnyDirty(): boolean {
    if (this.form?.dirty) return true;
    return this.isSectionDirty([
      'frequency', 'coreVoltage', 'autofanspeed', 'fanCurve', 'temptarget', 'vrrtarget', 'minfanspeed', 'manualFanSpeed',
      'dangerzone', 'ocFaultStep',
      'display', 'rotation', 'displayTimeout', 'invertscreen',
      'statsFrequency', 'heliosStatsEnabled'
    ]);
  }

  public applyAll(): void {
    this.applyField([
      'frequency', 'coreVoltage', 'autofanspeed', 'fanCurve', 'temptarget', 'vrrtarget', 'minfanspeed', 'manualFanSpeed',
      'dangerzone', 'ocFaultStep',
      'display', 'rotation', 'displayTimeout', 'invertscreen',
      'statsFrequency', 'heliosStatsEnabled'
    ]);
  }

  public applyField(fields: string[]): void {
    const raw = this.form.getRawValue();
    const patch: Record<string, any> = {};
    fields.forEach(f => { if (raw[f] !== undefined) patch[f] = raw[f]; });
    Object.assign(patch, this.extraFields);
    const deviceUri = this.uri || '';
    const prevValues: Record<string, any> = {};
    fields.forEach(f => { prevValues[f] = this.savedValues[f]; });
    this.systemService.updateSystem(deviceUri, patch)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          fields.forEach(f => {
            this.form.controls[f]?.markAsPristine();
            this.savedValues[f] = raw[f];
          });
          this.savedChanges = true;
          this.toastr.success(deviceUri ? `Saved settings for ${deviceUri}` : 'Saved');
          const needsRestart = fields
            .filter(f => raw[f] !== prevValues[f])
            .some(f => !this.noRestartFields.includes(f));
          if (needsRestart) this.restartRequired = true;
          this.sectionSaved.emit();
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error(deviceUri ? `Could not save settings for ${deviceUri}. ${err.message}` : `Could not save settings. ${err.message}`);
        }
      });
  }
}
