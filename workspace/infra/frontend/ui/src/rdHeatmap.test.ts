import {describe,it,expect} from 'vitest';
import {rdPixels,rdColor} from './rdHeatmap';
describe('RD rendering',()=>{
  it('maps range to x and doppler zero to the bottom',()=>{
    const result=rdPixels(new Float32Array([1,10,100,1000,1e4,1e8]),3,2,3,1,1e8);
    expect(Array.from(result.pixels.slice(0,4))).toEqual(rdColor(10,1,1e8));
    expect(Array.from(result.pixels.slice(12,16))).toEqual(rdColor(1,1,1e8));
    expect(Array.from(result.pixels.slice(8,12))).toEqual(rdColor(1e8,1,1e8));
  });
  it('preserves a narrow distance peak while reducing canvas width',()=>{
    const result=rdPixels(new Float32Array([1,1e8,1,1,1]),5,1,2,1,1e8);
    expect(Array.from(result.pixels.slice(0,4))).toEqual(rdColor(1e8,1,1e8));
    expect(result.width).toBe(2);
  });
  it('keeps log scale stable and marks invalid values',()=>{
    expect(rdColor(0,1,1e8)).toEqual(rdColor(1,1,1e8));
    expect(rdColor(-1,1,1e8)).toEqual([128,128,128,255]);
    expect(rdColor(NaN,1,1e8)).toEqual([128,128,128,255]);
    expect(new Set([1,1e3,1e5,1e8].map(v=>rdColor(v,1,1e8).join(','))).size).toBe(4);
  });
});
