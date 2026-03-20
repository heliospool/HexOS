import { Pipe, PipeTransform } from '@angular/core';

@Pipe({
  name: 'sats',
  pure: true
})
export class SatsPipe implements PipeTransform {
  private static _this = new SatsPipe();

  public static transform(value: number, coin: string = 'BTC'): string {
    return this._this.transform(value, coin);
  }

  transform(value: number, coin: string = 'BTC'): string {
    if (!value) return `0 ${coin}`;
    return (value / 100_000_000).toFixed(8) + ` ${coin}`;
  }
}
