#ifndef INTERFACE_H
#define INTERFACE_H

const char paginaHTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Vault</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" />
    <style>
        :root { --bg: #0f172a; --card: #1e293b; --primary: #38bdf8; --text: #f8fafc; --muted: #94a3b8; --danger: #ef4444; --success: #22c55e; --warn: #eab308; }
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: var(--bg); color: var(--text); margin: 0; padding: 20px; }
        .container { max-width: 1020px; margin: 0 auto; }
        h1 { text-align: center; color: var(--primary); margin-bottom: 20px; letter-spacing: 0.5px; }
        h2, h3 { color: var(--text); border-bottom: 1px solid #334155; padding-bottom: 8px; margin-top: 22px; }
        .tabs { display: flex; flex-wrap: wrap; gap: 6px; justify-content: center; margin-bottom: 20px; }
        .tab-btn { background: var(--card); border: 1px solid #334155; color: var(--text); padding: 10px 15px; cursor: pointer; border-radius: 6px; font-weight: bold; transition: 0.2s; width: auto; }
        .tab-btn.active, .tab-btn:hover { background: var(--primary); color: var(--bg); border-color: var(--primary); }
        .panel { display: none; background: var(--card); padding: 22px; border-radius: 10px; box-shadow: 0 4px 10px rgba(0,0,0,0.35); }
        .panel.active { display: block; }
        .metric-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(210px, 1fr)); gap: 14px; margin-top: 14px; }
        .card { background: #0f172a; padding: 14px; border-radius: 8px; border-left: 4px solid var(--primary); }
        .card h3 { margin: 0 0 6px 0; font-size: 13px; color: var(--muted); border: none; padding: 0; text-transform: uppercase; }
        .card p { margin: 0; font-size: 16px; font-weight: bold; color: var(--text); }
        .card small { color: var(--muted); font-size: 12px; display: block; margin-top: 4px; }
        table { width: 100%; border-collapse: collapse; margin-top: 10px; background: #0f172a; border-radius: 8px; overflow: hidden; }
        th, td { padding: 10px 12px; text-align: left; border-bottom: 1px solid #1e293b; font-size: 14px; }
        th { color: var(--primary); background: #090e1a; }
        input, select, button { padding: 10px; margin: 6px 0 12px 0; border-radius: 6px; border: 1px solid #334155; background: #0f172a; color: var(--text); width: 100%; box-sizing: border-box; }
        label { font-size: 14px; color: var(--muted); font-weight: 600; }
        button { background: var(--primary); color: var(--bg); font-weight: bold; cursor: pointer; border: none; }
        button:hover { opacity: 0.9; }
        .btn-inline { width: auto; padding: 8px 16px; margin-right: 8px; display: inline-block; }
        .btn-excel { background: var(--success); color: #052e16; }
        .btn-danger { background: var(--danger); color: #fff; }
        .badge { padding: 4px 8px; border-radius: 4px; font-size: 12px; font-weight: bold; }
        .badge-info { background: #0284c7; color: #fff; }
        .badge-warn { background: var(--warn); color: #000; }
        .badge-error { background: var(--danger); color: #fff; }
        #mapa-gps { height: 320px; width: 100%; border-radius: 8px; margin-top: 15px; z-index: 1; }
        .chart-container { position: relative; height: 240px; width: 100%; margin-top: 15px; background: #0f172a; padding: 14px; border-radius: 8px; box-sizing: border-box; }
        details.gaveta-24h { background: #0f172a; border: 1px solid #334155; border-radius: 8px; margin-top: 18px; padding: 12px 16px; }
        details.gaveta-24h summary { cursor: pointer; font-weight: bold; color: var(--primary); font-size: 15px; outline: none; list-style: none; display: flex; align-items: center; justify-content: space-between; }
        details.gaveta-24h summary::-webkit-details-marker { display: none; }
        details.gaveta-24h summary::after { content: "▼ Clique para expandir"; font-size: 12px; color: var(--muted); transition: 0.2s; }
        details.gaveta-24h[open] summary::after { content: "▲ Recolher histórico 24h"; color: var(--primary); }
        .form-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 14px; }
    </style>
</head>
<body>
<div class="container">
    <h1>Smart Vault</h1>
    <div class="tabs">
        <button class="tab-btn active" onclick="switchTab(event, 'painel')">Painel & Mapa</button>
        <button class="tab-btn" onclick="switchTab(event, 'sensores')">Dados dos Sensores</button>
        <button class="tab-btn" onclick="switchTab(event, 'performance')">Performance & Gráficos</button>
        <button class="tab-btn" onclick="switchTab(event, 'logs')">Logs (1000)</button>
        <button class="tab-btn" onclick="switchTab(event, 'config')">Configurações</button>
        <button class="tab-btn" onclick="switchTab(event, 'sobre')">Sobre</button>
        <button class="tab-btn" onclick="switchTab(event, 'changelog')">Changelog</button>
    </div>

    <!-- 1. PAINEL & MAPA GPS -->
    <div id="painel" class="panel active">
        <h2>Status Operacional e Geolocalização</h2>
        <div class="metric-grid">
            <div class="card"><h3>Estado da FSM</h3><p id="st-estado">Carregando...</p></div>
            <div class="card"><h3>Porta (Reed Switch)</h3><p id="st-porta">Carregando...</p></div>
            <div class="card"><h3>Tentativas Inválidas</h3><p id="st-tentativas">0 / 3</p></div>
            <div class="card"><h3>Coordenadas GPS</h3><p id="st-gps">Aguardando...</p></div>
        </div>
        <h3>Mapa de Localização em Tempo Real (GPS NEO-6M)</h3>
        <div id="mapa-gps"></div>
    </div>

    <!-- 2. DADOS DOS SENSORES -->
    <div id="sensores" class="panel">
        <h2>Dados dos Sensores (Último Valor Lido)</h2>
        <div class="metric-grid">
            <div class="card">
                <h3>Sensor Reed Switch (GPIO 4)</h3>
                <p id="sn-reed">-</p>
                <small id="sn-reed-sub">Aberturas registradas: 0</small>
            </div>
            <div class="card">
                <h3>Leitor MicroSD (SPI CS GPIO 5)</h3>
                <p id="sn-sd">-</p>
                <small id="sn-sd-sub">Total de verificações: 0</small>
            </div>
            <div class="card">
                <h3>Módulo GPS NEO-6M (UART2)</h3>
                <p id="sn-gps">-</p>
                <small id="sn-gps-sub">Bytes NMEA recebidos: 0</small>
            </div>
            <div class="card">
                <h3>Temperatura Interna do SoC</h3>
                <p id="sn-temp">-</p>
                <small>Leitura do sensor interno ESP32</small>
            </div>
        </div>

        <h3>Monitoramento dos Sensores em Tempo Real (Segundos)</h3>
        <div class="chart-container">
            <canvas id="graficoSensorTempRT"></canvas>
        </div>

        <!-- ABA OCULTA EXPANSIVEL: HISTORICO 24H DOS SENSORES -->
        <details class="gaveta-24h" ontoggle="redimensionarGraficos24h(this)">
            <summary>▶ Histórico dos Sensores das Últimas 24 Horas</summary>
            <div class="chart-container">
                <canvas id="graficoSensorTemp24h"></canvas>
            </div>
            <div class="chart-container">
                <canvas id="graficoSensorEv24h"></canvas>
            </div>
        </details>
    </div>

    <!-- 3. PERFORMANCE & GRAFICOS -->
    <div id="performance" class="panel">
        <h2>1. Processamento e Multitarefa (FreeRTOS)</h2>
        <div class="metric-grid">
            <div class="card">
                <h3>Ocupação de CPU (Tasks) / Clock</h3>
                <p id="pf-cpu">-</p>
                <small>Estimativa de carga ativa das Tasks</small>
            </div>
            <div class="card">
                <h3>Tasks / Threads Ativas</h3>
                <p id="pf-tasks">-</p>
                <small>TaskCofre (Core 1) | TaskRede (Core 0)</small>
            </div>
            <div class="card">
                <h3>Stack Livre (High Water Mark)</h3>
                <p id="pf-stack">-</p>
                <small>Alocado: 12288 B (Cofre) | 8192 B (Rede)</small>
            </div>
            <div class="card">
                <h3>Recurso OTA (Over-The-Air)</h3>
                <p id="pf-ota">Ativo (Porta 3232)</p>
                <small id="pf-ota-sub">Hostname: smart-vault.local</small>
            </div>
        </div>

        <h2>2. Uso de Memória (Heap, Flash e PSRAM)</h2>
        <div class="metric-grid">
            <div class="card">
                <h3>Memória RAM (Heap DRAM)</h3>
                <p id="pf-heap">-</p>
                <small id="pf-heap-min">Mínimo livre registrado: -</small>
            </div>
            <div class="card">
                <h3>Memória Flash (Programa / OTA)</h3>
                <p id="pf-flash">-</p>
                <small id="pf-flash-sub">Livre para OTA: -</small>
            </div>
            <div class="card">
                <h3>Memória PSRAM Externa</h3>
                <p id="pf-psram">-</p>
                <small id="pf-psram-sub">Status do barramento SPI RAM</small>
            </div>
        </div>

        <h2>3. Estado da Conexão Wi-Fi e Ponto de Acesso</h2>
        <div class="metric-grid">
            <div class="card">
                <h3>Estado & Ponto de Acesso (SSID)</h3>
                <p id="pf-wifi-ssid">-</p>
                <small id="pf-wifi-bssid">BSSID: -</small>
            </div>
            <div class="card">
                <h3>Nível do Sinal (RSSI)</h3>
                <p id="pf-rssi">-</p>
                <small id="pf-wifi-canal">Canal: -</small>
            </div>
            <div class="card">
                <h3>Endereço IP & Gateway</h3>
                <p id="pf-wifi-ip">-</p>
                <small id="pf-wifi-gw">Gateway: - | MAC: -</small>
            </div>
        </div>

        <h2>4. Tempo de Execução das 5 Funções Principais (Medições Independentes)</h2>
        <table>
            <tr><th>#</th><th>Função Principal do Sistema</th><th>Núcleo</th><th>Última Amostra (µs)</th></tr>
            <tr><td>1</td><td>TaskCofreCode() — Máquina de Estados (FSM) e Debounce (sem SD)</td><td>Core 1</td><td id="t-fsm">-</td></tr>
            <tr><td>2</td><td>lerEstadoCartaoSD() — Leitura SPI MicroSD e Validação AES-256</td><td>Core 1</td><td id="t-sd">-</td></tr>
            <tr><td>3</td><td>lerSerialGPS() — Parser NMEA 0183 via UART2</td><td>Core 0</td><td id="t-gps">-</td></tr>
            <tr><td>4</td><td>ArduinoOTA.handle() — Serviço de Atualização Over-The-Air</td><td>Core 0</td><td id="t-web">-</td></tr>
            <tr><td>5</td><td>gerenciarTelnet() — Servidor de Diagnóstico na Porta 23</td><td>Core 0</td><td id="t-telnet">-</td></tr>
        </table>

        <h2>5. Gráficos de Performance em Tempo Real (Segundos)</h2>
        <div class="chart-container">
            <canvas id="graficoHeap"></canvas>
        </div>
        <div class="chart-container">
            <canvas id="graficoRssi"></canvas>
        </div>

        <!-- ABA OCULTA EXPANSIVEL: HISTORICO 24H DE PERFORMANCE -->
        <details class="gaveta-24h" ontoggle="redimensionarGraficos24h(this)">
            <summary>▶ Histórico das Últimas 24 Horas (Memória Heap & Sinal Wi-Fi)</summary>
            <div class="chart-container">
                <canvas id="graficoHeap24h"></canvas>
            </div>
            <div class="chart-container">
                <canvas id="graficoRssi24h"></canvas>
            </div>
        </details>

        <h3>Gerenciamento de Energia</h3>
        <button onclick="ativarSleep()">Ativar Modo Light Sleep (10 Segundos)</button>
    </div>

    <!-- 4. LOGS PERSISTENTES NA FLASH -->
    <div id="logs" class="panel">
        <h2>Sistema de Logs Persistentes em Flash (LittleFS)</h2>
        <p style="color:var(--muted); font-size:14px;" id="info-qtd-logs">Registros gravados na Flash: carregando...</p>
        <div>
            <button class="btn-inline" onclick="carregarLogs()">Atualizar Tabela</button>
            <button class="btn-inline btn-excel" onclick="exportarLogsExcel()">Exportar para Excel (.CSV)</button>
            <button class="btn-inline btn-danger" onclick="limparLogsFlash()">Limpar Histórico da Flash</button>
        </div>
        <table>
            <thead><tr><th>Data e Hora / Timestamp</th><th>Nível</th><th>Mensagem do Evento</th></tr></thead>
            <tbody id="tabela-logs"><tr><td colspan="3">Carregando registros persistentes...</td></tr></tbody>
        </table>
    </div>

    <!-- 5. CONFIGURACOES VIA WEB -->
    <div id="config" class="panel">
        <h2>1. Frequência de Leitura dos Sensores e Parâmetros Operacionais</h2>
        <form onsubmit="salvarParametros(event)">
            <div class="form-grid">
                <div>
                    <label>Frequência de Leitura do Cartão SD (ms):</label>
                    <input type="number" id="cfg-f-sd" min="500" max="10000" value="1000" required>
                </div>
                <div>
                    <label>Período de Leitura do Reed Switch / FSM (ms):</label>
                    <input type="number" id="cfg-f-fsm" min="10" max="200" value="20" required>
                </div>
                <div>
                    <label>Intervalo de Amostragem dos Gráficos (ms):</label>
                    <input type="number" id="cfg-f-hist" min="1000" max="60000" value="2000" required>
                </div>
                <div>
                    <label>Máximo de Tentativas Inválidas (Alarme):</label>
                    <input type="number" id="cfg-m-tent" min="1" max="10" value="3" required>
                </div>
                <div>
                    <label>Tempo Máximo da Solenoide Acionada (1000 a 5000 ms):</label>
                    <input type="number" id="cfg-t-sol" min="1000" max="5000" value="4000" required>
                </div>
                <div>
                    <label>Duração do Aviso de Acesso Negado (ms):</label>
                    <input type="number" id="cfg-t-avi" min="1000" max="20000" value="5000" required>
                </div>
            </div>
            <label>Modo de Validação Criptográfica do Token SD:</label>
            <select id="cfg-q-tok">
                <option value="1">Flexível — Aceitar qualquer arquivo token.bin não vazio</option>
                <option value="0">Estrito — Exigir assinatura cifrada com AES-256-CBC</option>
            </select>
            <button type="submit">Salvar Parâmetros Operacionais na Flash (Aplicação Imediata)</button>
        </form>

        <h2>2. Configuração e Troca do Ponto de Acesso Wi-Fi (Sem Regravação)</h2>
        <form onsubmit="salvarWifi(event)">
            <label>SSID do Ponto de Acesso (Nome da Rede Wi-Fi):</label>
            <input type="text" id="cfg-ssid" maxlength="31" required>
            <label>Senha do Wi-Fi (Máx. 31 caracteres):</label>
            <input type="password" id="cfg-senha" maxlength="31" required>
            <button type="submit">Salvar Novo Wi-Fi na Flash NVS e Reiniciar</button>
        </form>
    </div>

    <!-- 6. SOBRE -->
    <div id="sobre" class="panel">
        <h2>Sobre o Projeto</h2>
        <p><strong>Universidade:</strong> Pontifícia Universidade Católica do Paraná</p>
        <p><strong>Curso:</strong> Engenharia de Software</p>
        <p><strong>Integrantes:</strong> Dominic SKiba, Pedro Matta, João Vicente Kochinsk, Luan Fadel e Leonardo Barbosa</p>
        <p><strong>Repositório GitHub:</strong> <a href="https://github.com/skibadominic" target="_blank" style="color:var(--primary)">https://github.com/skibadominic/smart-vault</a></p>
    </div>

    <!-- 7. CHANGELOG -->
    <div id="changelog" class="panel">
        <h2>Histórico de Alterações (Changelog)</h2>
        <ul>
            <li><strong>v2.6 (Novembro 2026):</strong> Isolamento das 5 medições de funções, stack de 12 KB na TaskCofre, proteção offline para CDNs e cabeçalho CSV UTF-8 validado.</li>
            <li><strong>v2.5:</strong> Persistência de 1000 logs na Flash via LittleFS com sincronização NTP e exportação para Excel (.CSV), aba de Dados dos Sensores e gavetas expansíveis 24h.</li>
            <li><strong>v2.0:</strong> Arquitetura multicore com FreeRTOS (TaskCofre no Core 1 e TaskRede no Core 0), gráficos em tempo real e mapa interativo GPS.</li>
            <li><strong>v1.0:</strong> Prototipagem inicial da FSM do cofre, validação de cartão MicroSD e acionamento de relés Active LOW.</li>
        </ul>
    </div>
</div>

<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<script>
let mapa = null;
let marcador = null;
let ultimaLat = null, ultimaLng = null;
let chartHeap = null, chartRssi = null, chartSensorTempRT = null;
let chartHeap24h = null, chartRssi24h = null, chartSensorTemp24h = null, chartSensorEv24h = null;

function inicializarMapa() {
    if (!mapa && typeof L !== 'undefined') {
        mapa = L.map('mapa-gps').setView([-25.4284, -49.2733], 15);
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
            attribution: '&copy; OpenStreetMap'
        }).addTo(mapa);
        marcador = L.marker([-25.4284, -49.2733]).addTo(mapa)
            .bindPopup("Smart Vault — Sem Fix GPS (Base Curitiba)").openPopup();
    }
}

function criarConfigLinha(label, cor, bg, zero) {
    return {
        type: 'line',
        data: { labels: [], datasets: [{ label: label, data: [], borderColor: cor, backgroundColor: bg, fill: true, tension: 0.3, spanGaps: false }] },
        options: { responsive: true, maintainAspectRatio: false, animation: false, scales: { y: { beginAtZero: zero } } }
    };
}

function inicializarGraficos() {
    if (typeof Chart === 'undefined') return;
    chartHeap = new Chart(document.getElementById('graficoHeap').getContext('2d'),
        criarConfigLinha('Heap Livre em Tempo Real (Bytes)', '#38bdf8', 'rgba(56, 189, 248, 0.12)', false));
    chartRssi = new Chart(document.getElementById('graficoRssi').getContext('2d'),
        criarConfigLinha('Sinal Wi-Fi RSSI em Tempo Real (dBm)', '#22c55e', 'rgba(34, 197, 94, 0.12)', false));
    chartSensorTempRT = new Chart(document.getElementById('graficoSensorTempRT').getContext('2d'),
        criarConfigLinha('Temperatura Interna do SoC em Tempo Real (°C)', '#f59e0b', 'rgba(245, 158, 11, 0.12)', false));

    chartHeap24h = new Chart(document.getElementById('graficoHeap24h').getContext('2d'),
        criarConfigLinha('Histórico 24h Medido — Memória Heap Livre (Bytes)', '#38bdf8', 'rgba(56, 189, 248, 0.12)', false));
    chartRssi24h = new Chart(document.getElementById('graficoRssi24h').getContext('2d'),
        criarConfigLinha('Histórico 24h Medido — Sinal Wi-Fi RSSI (dBm)', '#22c55e', 'rgba(34, 197, 94, 0.12)', false));
    chartSensorTemp24h = new Chart(document.getElementById('graficoSensorTemp24h').getContext('2d'),
        criarConfigLinha('Histórico 24h Medido — Temperatura do Sistema (°C)', '#f59e0b', 'rgba(245, 158, 11, 0.12)', false));

    chartSensorEv24h = new Chart(document.getElementById('graficoSensorEv24h').getContext('2d'), {
        type: 'bar',
        data: { labels: [], datasets: [{ label: 'Histórico 24h Medido — Eventos de Sensores por Hora', data: [], backgroundColor: '#38bdf8' }] },
        options: { responsive: true, maintainAspectRatio: false, animation: false, scales: { y: { beginAtZero: true } } }
    });
}

function redimensionarGraficos24h(el) {
    if (el.open) {
        setTimeout(() => {
            if (chartHeap24h) chartHeap24h.resize();
            if (chartRssi24h) chartRssi24h.resize();
            if (chartSensorTemp24h) chartSensorTemp24h.resize();
            if (chartSensorEv24h) chartSensorEv24h.resize();
            atualizarGraficosDinamicos();
        }, 60);
    }
}

function switchTab(evt, panelId) {
    document.querySelectorAll('.panel').forEach(p => p.classList.remove('active'));
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
    document.getElementById(panelId).classList.add('active');
    evt.currentTarget.classList.add('active');

    if (panelId === 'painel') setTimeout(() => { if (mapa) mapa.invalidateSize(); }, 200);
    if (panelId === 'logs') carregarLogs();
    if (panelId === 'config') carregarConfiguracoesAtuais();
    if (panelId === 'performance' || panelId === 'sensores') {
        carregarPerformance();
        atualizarGraficosDinamicos();
    }
}

function atualizarStatus() {
    fetch('/api/status').then(res => res.json()).then(data => {
        document.getElementById('st-estado').innerText = data.estado;
        document.getElementById('st-porta').innerText = data.porta;
        document.getElementById('st-tentativas').innerText = data.tentativas + " / " + data.max_tent;
        document.getElementById('st-gps').innerText = data.gps;

        document.getElementById('sn-reed').innerText = data.porta + " (GPIO4=" + data.pino_reed + ")";
        document.getElementById('sn-reed-sub').innerText = "Aberturas registradas: " + data.aberturas;
        document.getElementById('sn-sd').innerText = data.sd_status;
        document.getElementById('sn-sd-sub').innerText = "Verificações SPI realizadas: " + data.leituras_sd;
        document.getElementById('sn-gps').innerText = (data.fix ? "Fix Satélite Válido" : "Sem Fix (Base Curitiba)") + " (" + data.lat.toFixed(4) + ", " + data.lng.toFixed(4) + ")";
        document.getElementById('sn-gps-sub').innerText = "Bytes NMEA recebidos: " + data.bytes_gps;
        document.getElementById('sn-temp').innerText = data.temp_cpu.toFixed(1) + " °C";

        if (data.lat && data.lng && mapa && marcador) {
            let statusFix = data.fix ? "Fix GPS Real" : "Sem Fix GPS (Base Curitiba)";
            marcador.setPopupContent(`Smart Vault — ${statusFix}: ${data.lat.toFixed(5)}, ${data.lng.toFixed(5)}`);
            if (ultimaLat !== data.lat || ultimaLng !== data.lng) {
                ultimaLat = data.lat;
                ultimaLng = data.lng;
                let pos = [data.lat, data.lng];
                marcador.setLatLng(pos);
                mapa.panTo(pos);
            }
        }
    }).catch(err => console.log(err));
}

function carregarPerformance() {
    fetch('/api/performance').then(res => res.json()).then(d => {
        document.getElementById('pf-cpu').innerText = d.cpu_load.toFixed(2) + "% (" + d.cpu_freq + " MHz)";
        document.getElementById('pf-tasks').innerText = d.num_tasks + " Threads no Kernel (2 Tasks Principais)";
        document.getElementById('pf-stack').innerText = "Cofre: " + d.stack_cofre + " B | Rede: " + d.stack_rede + " B";
        document.getElementById('pf-ota').innerText = "Ativo (Porta " + d.ota_porta + ")";
        document.getElementById('pf-ota-sub').innerText = "Hostname: " + d.ota_hostname + ".local";

        document.getElementById('pf-heap').innerText = (d.heap_livre / 1024).toFixed(1) + " KB livres de " + (d.heap_total / 1024).toFixed(1) + " KB";
        document.getElementById('pf-heap-min').innerText = "Mínimo histórico: " + (d.heap_min / 1024).toFixed(1) + " KB (" + d.heap_livre + " B atuais)";

        document.getElementById('pf-flash').innerText = (d.flash_usada / 1024).toFixed(0) + " KB usados / " + (d.flash_total / (1024*1024)).toFixed(0) + " MB Total";
        document.getElementById('pf-flash-sub').innerText = "Espaço livre para OTA: " + (d.flash_livre / 1024).toFixed(0) + " KB";

        if (d.psram_total > 0) {
            document.getElementById('pf-psram').innerText = (d.psram_livre / 1024).toFixed(0) + " KB livres / " + (d.psram_total / 1024).toFixed(0) + " KB";
            document.getElementById('pf-psram-sub').innerText = "PSRAM Ativa";
        } else {
            document.getElementById('pf-psram').innerText = "0 KB (Não presente no módulo WROOM)";
            document.getElementById('pf-psram-sub').innerText = "Operando 100% em SRAM Interna";
        }

        document.getElementById('pf-wifi-ssid').innerText = d.wifi_status + " — " + d.ssid;
        document.getElementById('pf-wifi-bssid').innerText = "BSSID AP: " + d.bssid;
        document.getElementById('pf-rssi').innerText = d.rssi + " dBm (Qualidade: " + d.qualidade + "%)";
        document.getElementById('pf-wifi-canal').innerText = "Canal Wi-Fi: " + d.canal;
        document.getElementById('pf-wifi-ip').innerText = d.ip;
        document.getElementById('pf-wifi-gw').innerText = "GW: " + d.gw + " | MAC: " + d.mac;

        document.getElementById('t-fsm').innerText = d.t_fsm + " µs";
        document.getElementById('t-sd').innerText = d.t_sd + " µs";
        document.getElementById('t-gps').innerText = d.t_gps + " µs";
        document.getElementById('t-web').innerText = d.t_web + " µs";
        document.getElementById('t-telnet').innerText = d.t_telnet + " µs";
    }).catch(err => console.log(err));
}

function atualizarGraficosDinamicos() {
    fetch('/api/historico').then(res => res.json()).then(d => {
        if (chartHeap && chartRssi && chartSensorTempRT) {
            chartHeap.data.labels = d.labels;
            chartHeap.data.datasets[0].data = d.heap;
            chartHeap.update('none');

            chartRssi.data.labels = d.labels;
            chartRssi.data.datasets[0].data = d.rssi;
            chartRssi.update('none');

            chartSensorTempRT.data.labels = d.labels;
            chartSensorTempRT.data.datasets[0].data = d.temp;
            chartSensorTempRT.update('none');
        }
        if (d.labels24h && chartHeap24h) {
            chartHeap24h.data.labels = d.labels24h;
            chartHeap24h.data.datasets[0].data = d.heap24h;
            chartHeap24h.update('none');

            chartRssi24h.data.labels = d.labels24h;
            chartRssi24h.data.datasets[0].data = d.rssi24h;
            chartRssi24h.update('none');

            chartSensorTemp24h.data.labels = d.labels24h;
            chartSensorTemp24h.data.datasets[0].data = d.temp24h;
            chartSensorTemp24h.update('none');

            chartSensorEv24h.data.labels = d.labels24h;
            chartSensorEv24h.data.datasets[0].data = d.ev24h;
            chartSensorEv24h.update('none');
        }
    }).catch(err => console.log(err));
}

function carregarLogs() {
    fetch('/api/logs').then(res => res.json()).then(data => {
        document.getElementById('info-qtd-logs').innerText =
            `Registros persistidos na Flash (LittleFS): ${data.total_flash} / ${data.max_flash} (Exibindo os últimos ${data.itens.length} em tela — Exporte para Excel para ver todos)`;
        let tbody = document.getElementById('tabela-logs');
        tbody.innerHTML = '';
        data.itens.reverse().forEach(l => {
            let badgeClass = l.tipo === 'ERROR' ? 'badge-error' : (l.tipo === 'WARN' ? 'badge-warn' : 'badge-info');
            tbody.innerHTML += `<tr><td>${l.time}</td><td><span class="badge ${badgeClass}">${l.tipo}</span></td><td>${l.msg}</td></tr>`;
        });
    }).catch(err => console.log(err));
}

function exportarLogsExcel() {
    window.location.href = '/api/logs/export';
}

function limparLogsFlash() {
    if (confirm("Deseja realmente apagar os logs gravados na memória Flash?")) {
        fetch('/api/logs/clear').then(res => res.text()).then(msg => {
            alert(msg);
            carregarLogs();
        });
    }
}

function carregarConfiguracoesAtuais() {
    fetch('/api/config/get').then(res => res.json()).then(c => {
        document.getElementById('cfg-f-sd').value = c.f_sd;
        document.getElementById('cfg-f-fsm').value = c.f_fsm;
        document.getElementById('cfg-f-hist').value = c.f_hist;
        document.getElementById('cfg-m-tent').value = c.m_tent;
        document.getElementById('cfg-t-sol').value = c.t_sol;
        document.getElementById('cfg-t-avi').value = c.t_avi;
        document.getElementById('cfg-q-tok').value = c.q_tok ? "1" : "0";
        document.getElementById('cfg-ssid').value = c.ssid;
    }).catch(err => console.log(err));
}

function salvarParametros(e) {
    e.preventDefault();
    let qs = new URLSearchParams({
        f_sd: document.getElementById('cfg-f-sd').value,
        f_fsm: document.getElementById('cfg-f-fsm').value,
        f_hist: document.getElementById('cfg-f-hist').value,
        m_tent: document.getElementById('cfg-m-tent').value,
        t_sol: document.getElementById('cfg-t-sol').value,
        t_avi: document.getElementById('cfg-t-avi').value,
        q_tok: document.getElementById('cfg-q-tok').value
    }).toString();
    fetch('/api/config/parametros?' + qs).then(res => res.text()).then(msg => alert(msg));
}

function salvarWifi(e) {
    e.preventDefault();
    let ssid = document.getElementById('cfg-ssid').value;
    let senha = document.getElementById('cfg-senha').value;
    fetch(`/api/config?ssid=${encodeURIComponent(ssid)}&senha=${encodeURIComponent(senha)}`)
        .then(res => res.text()).then(msg => alert(msg));
}

function ativarSleep() {
    fetch('/api/energiasleep').then(res => res.text()).then(msg => alert(msg));
}

window.onload = function() {
    // Executa e agenda a telemetria antes das bibliotecas de CDN para nunca travar offline
    atualizarStatus();
    setInterval(atualizarStatus, 2000);

    try { inicializarMapa(); } catch (e) { console.log("Leaflet CDN indisponivel:", e); }
    try { inicializarGraficos(); } catch (e) { console.log("Chart.js CDN indisponivel:", e); }

    setInterval(() => {
        let perfAtivo = document.getElementById('performance').classList.contains('active');
        let sensAtivo = document.getElementById('sensores').classList.contains('active');
        if (perfAtivo || sensAtivo) {
            if (perfAtivo) carregarPerformance();
            atualizarGraficosDinamicos();
        }
    }, 2000);
};
</script>
</body>
</html>
)rawliteral";

#endif
