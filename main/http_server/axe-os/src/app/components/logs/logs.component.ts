import { AfterViewChecked, Component, Input, OnInit, ElementRef, OnDestroy, ViewChild, HostListener } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { Subscription } from 'rxjs';
import { ToastrService } from 'ngx-toastr';
import { WebsocketService } from 'src/app/services/web-socket.service';
import { SystemApiService } from 'src/app/services/system.service';
import { SystemInfo } from 'src/app/generated/model/system-info';

@Component({
  selector: 'app-logs',
  templateUrl: './logs.component.html',
  styleUrl: './logs.component.scss'
})
export class LogsComponent implements OnInit, OnDestroy, AfterViewChecked {

  public form!: FormGroup;

  public logs: { className: string, text: string }[] = [];

  private websocketSubscription?: Subscription;

  public stopScroll: boolean = false;

  public isExpanded: boolean = false;

  public maxLogs: number = 500;

  public debugLog: boolean = false;

  @ViewChild('scrollContainer') private scrollContainer!: ElementRef;

  @HostListener('document:keydown.esc', ['$event'])
  onEscKey() {
    if (this.isExpanded) {
      this.isExpanded = false;
    }
  }

  @Input() uri = '';

  constructor(
    private fb: FormBuilder,
    private websocketService: WebsocketService,
    private toastr: ToastrService,
    private systemService: SystemApiService,
  ) {}

  ngOnInit(): void {
    this.subscribeLogs();

    this.form = this.fb.group({
      filter: ["", [Validators.required]],
      maxLogs: [500, [Validators.required, Validators.min(50), Validators.max(5000)]]
    });

    this.systemService.getInfo(this.uri).subscribe({
      next: (info: SystemInfo) => {
        this.debugLog = info.debugLog ?? false;
      },
      error: () => {}
    });
  }

  ngOnDestroy(): void {
    this.websocketSubscription?.unsubscribe();
    this.clearLogs();
  }

  private subscribeLogs() {
    this.websocketSubscription = this.websocketService.ws$.subscribe({
        next: (val) => {
          const matches = val.matchAll(/\[(\d+;\d+)m(.*?)(?=\[|\n|$)/g);
          let className = 'ansi-white'; // default color

          for (const match of matches) {
            const colorCode = match[1].split(';')[1];
            switch (colorCode) {
              case '31': className = 'ansi-red'; break;
              case '32': className = 'ansi-green'; break;
              case '33': className = 'ansi-yellow'; break;
              case '34': className = 'ansi-blue'; break;
              case '35': className = 'ansi-magenta'; break;
              case '36': className = 'ansi-cyan'; break;
              case '37': className = 'ansi-white'; break;
            }
          }

          // Get current filter value from form
          const currentFilter = this.form?.get('filter')?.value;

          if (!currentFilter || val.includes(currentFilter)) {
            this.logs.push({ className: `max-w-full text-monospace ${className}`, text: val });
          }

          const maxLogs = this.form?.get('maxLogs')?.value || 500;
          if (this.logs.length > maxLogs) {
            this.logs.shift();
          }
        },
        error: (error) => {
          this.toastr.error("Error opening websocket connection");
        }
      })
  }

  public onDebugLogChange(value: boolean) {
    this.systemService.updateSystem(this.uri, { debugLog: value }).subscribe({
      next: () => { this.toastr.success(`Debug logging ${value ? 'enabled' : 'disabled'}`); },
      error: () => { this.toastr.error('Failed to update debug logging'); this.debugLog = !value; }
    });
  }

  public clearLogs() {
    this.logs.length = 0;
  }

  public downloadLogs() {
    const ansiRegex = /\x1B+\[[0-9;]*m/g;
    const content = this.logs.map(l => l.text.replace(ansiRegex, '').trimEnd()).join('\n');
    const blob = new Blob([content], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    const ts = new Date().toISOString().replace(/[:.]/g, '-');
    a.href = url;
    a.download = `hexos-logs-${ts}.txt`;
    a.click();
    URL.revokeObjectURL(url);
  }

  ngAfterViewChecked(): void {
    if(this.stopScroll == true){
      return;
    }
    if (this.scrollContainer?.nativeElement != null) {
      this.scrollContainer.nativeElement.scrollTo({ left: 0, top: this.scrollContainer.nativeElement.scrollHeight, behavior: 'smooth' });
    }
  }

}
