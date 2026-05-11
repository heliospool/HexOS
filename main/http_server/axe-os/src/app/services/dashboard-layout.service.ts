import { Injectable } from '@angular/core';
import { LocalStorageService } from '../local-storage.service';

export type ColClass = 'col-3' | 'col-4' | 'col-6' | 'col-12' | null;

export interface DashboardRow {
  id: string;
  colClass: ColClass;
  cardIds: string[];
}

export interface LayoutData {
  rows: DashboardRow[];
  hiddenCardIds: string[];
}

export interface CardDef {
  id: string;
  label: string;
  colClass: ColClass;
  conditional: boolean;
}

export const CARD_DEFS: CardDef[] = [
  { id: 'hp-server',     label: 'HeliosPool — Server',    colClass: 'col-3',  conditional: true  },
  { id: 'hp-status',     label: 'HeliosPool — Status',    colClass: 'col-3',  conditional: true  },
  { id: 'hp-account',    label: 'HeliosPool — Account',   colClass: 'col-3',  conditional: true  },
  { id: 'hp-worker',     label: 'HeliosPool — Worker',    colClass: 'col-3',  conditional: true  },
  { id: 'hashrate',      label: 'Hashrate',               colClass: 'col-3',  conditional: false },
  { id: 'efficiency',    label: 'Efficiency',             colClass: 'col-3',  conditional: false },
  { id: 'best-diff',     label: 'Best Difficulty',        colClass: 'col-3',  conditional: false },
  { id: 'chart',         label: 'Chart',                  colClass: 'col-12', conditional: true  },
  { id: 'power',         label: 'Power',                  colClass: 'col-4',  conditional: false },
  { id: 'asic',          label: 'ASIC',                   colClass: 'col-4',  conditional: false },
  { id: 'heat',          label: 'Heat',                   colClass: 'col-4',  conditional: false },
  { id: 'fan',           label: 'Fan',                    colClass: 'col-4',  conditional: false },
  { id: 'hashrate-regs', label: 'Hashrate Registers',     colClass: 'col-4',  conditional: false },
  { id: 'network',       label: 'Network',                colClass: 'col-3',  conditional: false },
  { id: 'pool',          label: 'Pool',                   colClass: 'col-4',  conditional: false },
  { id: 'block-header',  label: 'Block Header',           colClass: 'col-4',  conditional: false },
  { id: 'blocks',        label: 'Blocks Found',           colClass: 'col-4',  conditional: false },
  { id: 'pipeline',      label: 'Pipeline',               colClass: 'col-12', conditional: false },
];

const CARD_MAP = new Map(CARD_DEFS.map(c => [c.id, c]));

const DEFAULT_ROWS: DashboardRow[] = [
  { id: 'row-hp',       colClass: 'col-3',  cardIds: ['hp-server', 'hp-status', 'hp-account', 'hp-worker'] },
  { id: 'row-stats',    colClass: 'col-3',  cardIds: ['hashrate', 'efficiency', 'best-diff', 'network'] },
  { id: 'row-pipeline', colClass: 'col-12', cardIds: ['pipeline'] },
  { id: 'row-chart',    colClass: 'col-12', cardIds: ['chart'] },
  { id: 'row-detail-a', colClass: 'col-4',  cardIds: ['power', 'asic', 'hashrate-regs'] },
  { id: 'row-detail-b', colClass: 'col-4',  cardIds: ['heat', 'fan'] },
  { id: 'row-detail-c', colClass: 'col-4',  cardIds: ['pool', 'block-header', 'blocks'] },
];

const STORAGE_KEY = 'DASHBOARD_LAYOUT_v6';

@Injectable({ providedIn: 'root' })
export class DashboardLayoutService {

  constructor(private storage: LocalStorageService) {}

  getCardDef(id: string): CardDef | undefined {
    return CARD_MAP.get(id);
  }

  getCardLabel(id: string): string {
    return CARD_MAP.get(id)?.label ?? id;
  }

  rowCapacity(colClass: ColClass): number {
    switch (colClass) {
      case 'col-3':  return 4;
      case 'col-4':  return 3;
      case 'col-6':  return 2;
      case 'col-12': return 1;
      default:       return 4;
    }
  }

  cloneDefault(): DashboardRow[] {
    return DEFAULT_ROWS.map(r => ({ ...r, cardIds: [...r.cardIds] }));
  }

  loadLayout(): LayoutData {
    try {
      const saved = this.storage.getObject(STORAGE_KEY);
      if (!saved || !this.isValidLayout(saved)) return this.defaultLayout();
      const rows = this.reconcile(saved.rows as DashboardRow[]);
      const hiddenCardIds = Array.isArray(saved.hiddenCardIds)
        ? (saved.hiddenCardIds as any[]).filter((id: any) => typeof id === 'string' && CARD_MAP.has(id))
        : [];
      return { rows, hiddenCardIds };
    } catch {
      return this.defaultLayout();
    }
  }

  saveLayout(rows: DashboardRow[], hiddenCardIds: string[]): void {
    const toSave = {
      rows: rows.map(r => ({ id: r.id, colClass: r.colClass, cardIds: [...r.cardIds] })),
      hiddenCardIds: [...hiddenCardIds],
    };
    this.storage.setObject(STORAGE_KEY, toSave);
  }

  resetToDefault(): LayoutData {
    localStorage.removeItem(STORAGE_KEY);
    return this.defaultLayout();
  }

  private defaultLayout(): LayoutData {
    return { rows: this.cloneDefault(), hiddenCardIds: [] };
  }

  generateRowId(): string {
    return 'row-' + Math.random().toString(36).slice(2, 9);
  }

  private isValidLayout(saved: any): boolean {
    if (!saved || typeof saved !== 'object' || !Array.isArray(saved.rows)) return false;
    const validColClasses = new Set<string | null>(['col-3', 'col-4', 'col-6', 'col-12', null]);
    return saved.rows.every((r: any) =>
      r &&
      typeof r.id === 'string' &&
      validColClasses.has(r.colClass) &&
      Array.isArray(r.cardIds) &&
      r.cardIds.every((id: any) => typeof id === 'string' && CARD_MAP.has(id))
    );
  }

  private reconcile(saved: DashboardRow[]): DashboardRow[] {
    const usedIds = new Set(saved.flatMap(r => r.cardIds));
    const missing = CARD_DEFS.filter(c => !usedIds.has(c.id));
    if (missing.length === 0) return saved;
    const result = saved.map(r => ({ ...r, cardIds: [...r.cardIds] }));
    for (const card of missing) {
      const targetRow = result.find(r => r.colClass === card.colClass);
      if (targetRow) {
        targetRow.cardIds.push(card.id);
      } else {
        result.push({ id: `row-${card.colClass}-${Date.now()}`, colClass: card.colClass, cardIds: [card.id] });
      }
    }
    return result;
  }
}
