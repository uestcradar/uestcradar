// Viridis anchors; interpolation keeps the color mapping stable across frames.
const palette = [[68,1,84],[71,44,122],[59,81,139],[44,113,142],[33,144,141],[39,173,129],[92,200,99],[170,220,50],[253,231,37]];
export function rdColor(value: number, low: number, high: number): number[] {
  if (!Number.isFinite(value) || value < 0) return [128,128,128,255];
  const t = value === 0 ? 0 : Math.max(0, Math.min(1, (Math.log10(value)-Math.log10(low))/(Math.log10(high)-Math.log10(low))));
  const p = t*(palette.length-1), i = Math.min(palette.length-2, Math.floor(p)), f = p-i;
  return [...palette[i].map((v,c) => Math.round(v+(palette[i+1][c]-v)*f)),255];
}
export function rdPixels(values: Float32Array, ranges: number, dopplers: number, width: number, low: number, high: number) {
  width = Math.max(1, Math.min(ranges, Math.floor(width)));
  const pixels = new Uint8ClampedArray(width*dopplers*4);
  for (let x=0; x<width; x++) {
    const begin = Math.floor(x*ranges/width), end = Math.floor((x+1)*ranges/width);
    for (let d=0; d<dopplers; d++) {
      let best = Number.NaN;
      for (let r=begin; r<end; r++) {
        const value = values[r*dopplers+d];
        if (Number.isFinite(value) && (!Number.isFinite(best) || value>best)) best=value;
      }
      pixels.set(rdColor(best,low,high), ((dopplers-1-d)*width+x)*4);
    }
  }
  return {width, height: dopplers, pixels};
}
