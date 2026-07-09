%% generate_lfm_tx.m
% MATLAB script to generate a single-pulse Linear Frequency Modulation (LFM) 
% baseband IQ waveform, formatted as CS16 (interleaved signed 16-bit integers),
% along with a metadata.json file consumed by the cycore backend.

clear; clc;

%% 1. Configure Waveform Parameters
outDir = 'lfm_tx';
if ~exist(outDir, 'dir')
    mkdir(outDir);
end

sampleRate = 30.72e6;        % 30.72 MHz
pri = 4096;                  % PRI length in samples
activeSamples = 64;         % Active chirp duration (PW = 64 samples)
bandwidth = 10e6;            % Sweep bandwidth (10 MHz)
amplitude = 0.7;             % Amplitude fraction (0.7 of full-scale int16)
numPulses = 1;               % Output a single pulse reference signal

%% 2. Generate Baseband LFM Chirp
% Time vector for the active pulse width
t = (0 : activeSamples - 1)' / sampleRate;

% Active chirp duration in seconds
T_pulse = activeSamples / sampleRate;

% Chirp rate (slope)
K = bandwidth / T_pulse;

% Phase equation: Sweeping from -B/2 to +B/2 (symmetric baseband sweep)
phi = 2 * pi * (-bandwidth/2 * t + 0.5 * K * t.^2);

% Generate complex baseband signal
chirp = exp(1i * phi);

% Assemble full PRI-long pulse (pad remaining samples with zeros)
pulse = zeros(pri, 1);
pulse(1:activeSamples) = chirp;

%% 3. Quantize and Format to CS16 (Interleaved Real/Imag Int16)
% Scale to 16-bit integer limit (32767)
scaledI = round(real(pulse) * amplitude * 32767);
scaledQ = round(imag(pulse) * amplitude * 32767);

% Clip to prevent potential overflows
scaledI = max(-32767, min(32767, scaledI));
scaledQ = max(-32767, min(32767, scaledQ));

% Interleave I and Q components
iqData = zeros(2 * pri, 1);
iqData(1:2:end) = scaledI;
iqData(2:2:end) = scaledQ;

%% 4. Save Binary File
binPath = fullfile(outDir, 'lfm_tx.bin');
fprintf('Writing LFM binary waveform to: %s\n', binPath);
fid_bin = fopen(binPath, 'wb');
if fid_bin == -1
    error('Cannot open output file: %s', binPath);
end

% Write raw int16 values in little-endian format
for p = 1:numPulses
    fwrite(fid_bin, iqData, 'int16', 0, 'ieee-le');
end
fclose(fid_bin);

%% 5. Save metadata.json
metadataPath = fullfile(outDir, 'metadata.json');
fprintf('Writing metadata to: %s\n', metadataPath);

metadataStr = sprintf(['{\n', ...
    '  "format": "CS16",\n', ...
    '  "PRI": %d,\n', ...
    '  "channels": [0],\n', ...
    '  "sample_rate": %.1f,\n', ...
    '  "waveform": {\n', ...
    '    "type": "LFM",\n', ...
    '    "bandwidth_mhz": %.1f,\n', ...
    '    "PW": %d,\n', ...
    '    "num_pulses": %d,\n', ...
    '    "amplitude": %.2f\n', ...
    '  }\n', ...
    '}'], pri, sampleRate, bandwidth / 1e6, activeSamples, numPulses, amplitude);

fid_meta = fopen(metadataPath, 'w');
if fid_meta == -1
    error('Cannot open metadata file: %s', metadataPath);
end
fprintf(fid_meta, '%s', metadataStr);
fclose(fid_meta);

fprintf('Waveform generation complete.\n');
