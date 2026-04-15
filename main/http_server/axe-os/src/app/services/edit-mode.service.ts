import { Injectable } from '@angular/core';
import { BehaviorSubject } from 'rxjs';

@Injectable({ providedIn: 'root' })
export class EditModeService {
  private readonly _editMode = new BehaviorSubject<boolean>(false);
  readonly editMode$ = this._editMode.asObservable();

  get isActive(): boolean {
    return this._editMode.value;
  }

  toggle(): void {
    this._editMode.next(!this._editMode.value);
  }

  exit(): void {
    this._editMode.next(false);
  }
}
