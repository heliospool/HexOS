import { NgModule } from '@angular/core';
import { RadioButtonModule } from 'primeng/radiobutton';
import { ButtonModule } from 'primeng/button';
import { ChartModule } from 'primeng/chart';
import { CheckboxModule } from 'primeng/checkbox';
import { DropdownModule } from 'primeng/dropdown';
import { FileUploadModule } from 'primeng/fileupload';
import { InputGroupModule } from 'primeng/inputgroup';
import { InputGroupAddonModule } from 'primeng/inputgroupaddon';
import { InputTextModule } from 'primeng/inputtext';
import { InputNumberModule } from 'primeng/inputnumber';
import { SidebarModule } from 'primeng/sidebar';
import { SliderModule } from 'primeng/slider';
import { InputTextareaModule } from 'primeng/inputtextarea';
import { InputSwitchModule } from 'primeng/inputswitch';

const primeNgModules = [
    SidebarModule,
    InputTextModule,
    InputNumberModule,
    CheckboxModule,
    DropdownModule,
    SliderModule,
    ButtonModule,
    FileUploadModule,
    ChartModule,
    InputGroupModule,
    InputGroupAddonModule,
    RadioButtonModule,
    InputTextareaModule,
    InputSwitchModule,
];

@NgModule({
    imports: [
        ...primeNgModules
    ],
    exports: [
        ...primeNgModules
    ],
})
export class PrimeNGModule { }
