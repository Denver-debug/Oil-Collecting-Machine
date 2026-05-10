/*
 * ESP32 Unified Tank Controller
 * - Mobility: Dual BTS7960 motor drivers with circular joystick
 * - Auxiliary: Pump and Aux Motor via MOSFET modules
 * - Landscape layout with circular toggle buttons
 * 
 * MOBILITY WIRING (BTS7960):
 * Left Motor:  RPWM=25, LPWM=26, R_EN=27, L_EN=13
 * Right Motor: RPWM=32, LPWM=33, R_EN=18, L_EN=19
 * 
 * AUXILIARY WIRING (MOSFET):
 * Pump:      TRIG/PWM -> GPIO 14, GND -> ESP32 GND
 * Aux Motor: TRIG/PWM -> GPIO 12, GND -> ESP32 GND
 */

#include <WiFi.h>
#include <WebServer.h>

// WiFi credentials
const char* ssid = "Galaxy A52s 5GB1D0";
const char* password = "upui9397";

// ===== MOBILITY MOTORS (BTS7960) =====
// Left Motor (BTS7960 Driver #1)
const int RPWM1_PIN = 25;
const int LPWM1_PIN = 26;
const int R_EN1_PIN = 27;
const int L_EN1_PIN = 13;

// Right Motor (BTS7960 Driver #2)
const int RPWM2_PIN = 32;
const int LPWM2_PIN = 33;
const int R_EN2_PIN = 18;
const int L_EN2_PIN = 19;

// ===== AUXILIARY DEVICES (MOSFET) =====
const int PUMP_PIN = 14;      // Pump MOSFET control
const int AUX_MOTOR_PIN = 12; // Aux Motor MOSFET control

// PWM settings for mobility motors
const int PWM_FREQ = 20000;
const int PWM_RESOLUTION = 8;

// Motor tuning
const int DEADZONE = 10;
const int PWM_MIN = 60;
const int PWM_MAX = 160;
const int RAMP_STEP = 1;
const unsigned long RAMP_INTERVAL = 25;

WebServer server(80);

// Mobility motor states
int targetSpeed1 = 0;
int currentSpeed1 = 0;
unsigned long lastRampTime1 = 0;

int targetSpeed2 = 0;
int currentSpeed2 = 0;
unsigned long lastRampTime2 = 0;

// Auxiliary device states
bool pumpState = false;
bool auxMotorState = false;

// Helper function
int signOf(int value) {
    if (value > 0) return 1;
    if (value < 0) return -1;
    return 0;
}

// Apply speed to mobility motor
void applyMotor(int motorNum, int speed) {
    speed = constrain(speed, -100, 100);
    
    int rpwm_pin, lpwm_pin, r_en_pin, l_en_pin;
    
    if (motorNum == 1) {
        rpwm_pin = RPWM1_PIN;
        lpwm_pin = LPWM1_PIN;
        r_en_pin = R_EN1_PIN;
        l_en_pin = L_EN1_PIN;
    } else {
        rpwm_pin = RPWM2_PIN;
        lpwm_pin = LPWM2_PIN;
        r_en_pin = R_EN2_PIN;
        l_en_pin = L_EN2_PIN;
    }
    
    digitalWrite(r_en_pin, HIGH);
    digitalWrite(l_en_pin, HIGH);
    
    if (speed == 0) {
        ledcWrite(rpwm_pin, 0);
        ledcWrite(lpwm_pin, 0);
        return;
    }
    
    int pwmValue = map(abs(speed), DEADZONE, 100, PWM_MIN, PWM_MAX);
    pwmValue = constrain(pwmValue, PWM_MIN, PWM_MAX);
    
    if (speed > 0) {
        ledcWrite(lpwm_pin, 0);
        delayMicroseconds(200);
        ledcWrite(rpwm_pin, pwmValue);
    } else {
        ledcWrite(rpwm_pin, 0);
        delayMicroseconds(200);
        ledcWrite(lpwm_pin, pwmValue);
    }
}

void stopMotor(int motorNum) {
    if (motorNum == 1) {
        targetSpeed1 = 0;
        currentSpeed1 = 0;
        applyMotor(1, 0);
    } else {
        targetSpeed2 = 0;
        currentSpeed2 = 0;
        applyMotor(2, 0);
    }
}

void stopAllMotors() {
    stopMotor(1);
    stopMotor(2);
}

void setMotorSpeed(int motorNum, int speed) {
    speed = constrain(speed, -100, 100);
    
    if (speed != 0 && abs(speed) < DEADZONE) {
        speed = 0;
    }
    
    if (motorNum == 1) {
        targetSpeed1 = speed;
    } else {
        targetSpeed2 = speed;
    }
}

void updateMotorRamp(int motorNum) {
    int *currentSpeed, *targetSpeed;
    unsigned long *lastRampTime;
    
    if (motorNum == 1) {
        currentSpeed = &currentSpeed1;
        targetSpeed = &targetSpeed1;
        lastRampTime = &lastRampTime1;
    } else {
        currentSpeed = &currentSpeed2;
        targetSpeed = &targetSpeed2;
        lastRampTime = &lastRampTime2;
    }
    
    if (*currentSpeed == *targetSpeed) return;
    
    unsigned long now = millis();
    if (now - *lastRampTime < RAMP_INTERVAL) return;
    
    *lastRampTime = now;
    
    int currentSign = signOf(*currentSpeed);
    int targetSign = signOf(*targetSpeed);
    
    if (currentSign != 0 && targetSign != 0 && currentSign != targetSign) {
        if (*currentSpeed > 0) {
            *currentSpeed = max(0, *currentSpeed - RAMP_STEP);
        } else {
            *currentSpeed = min(0, *currentSpeed + RAMP_STEP);
        }
    } else {
        if (abs(*targetSpeed - *currentSpeed) <= RAMP_STEP) {
            *currentSpeed = *targetSpeed;
        } else if (*targetSpeed > *currentSpeed) {
            *currentSpeed += RAMP_STEP;
        } else {
            *currentSpeed -= RAMP_STEP;
        }
    }
    
    applyMotor(motorNum, *currentSpeed);
}

// Web page handler
void handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>Tank Controller</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            -webkit-tap-highlight-color: transparent;
        }
        
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #1e3c72 0%, #2a5298 100%);
            overflow: hidden;
            touch-action: none;
        }
        
        .landscape-container {
            display: flex;
            height: 100vh;
            width: 100vw;
            padding: 15px;
            gap: 15px;
        }
        
        /* Left side - Joystick */
        .joystick-section {
            flex: 1;
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
            background: rgba(255, 255, 255, 0.1);
            border-radius: 20px;
            backdrop-filter: blur(10px);
            padding: 20px;
        }
        
        .joystick-title {
            color: white;
            font-size: 20px;
            font-weight: bold;
            margin-bottom: 15px;
            text-shadow: 2px 2px 4px rgba(0,0,0,0.3);
        }
        
        .joystick-container {
            position: relative;
            width: 280px;
            height: 280px;
            border-radius: 50%;
            background: rgba(255, 255, 255, 0.15);
            box-shadow: inset 0 8px 20px rgba(0,0,0,0.3);
            touch-action: none;
        }
        
        .joystick-center {
            position: absolute;
            top: 50%;
            left: 50%;
            width: 20px;
            height: 20px;
            transform: translate(-50%, -50%);
            border-radius: 50%;
            background: rgba(255, 255, 255, 0.3);
        }
        
        .joystick-stick {
            position: absolute;
            top: 50%;
            left: 50%;
            width: 90px;
            height: 90px;
            transform: translate(-50%, -50%);
            border-radius: 50%;
            background: linear-gradient(145deg, #667eea, #764ba2);
            box-shadow: 0 8px 20px rgba(0,0,0,0.4);
            cursor: grab;
            z-index: 10;
        }
        
        .joystick-stick:active {
            cursor: grabbing;
        }
        
        .motor-info {
            display: flex;
            gap: 20px;
            margin-top: 20px;
        }
        
        .motor-display {
            background: rgba(255, 255, 255, 0.2);
            padding: 15px 25px;
            border-radius: 12px;
            text-align: center;
        }
        
        .motor-label {
            color: rgba(255, 255, 255, 0.8);
            font-size: 12px;
            margin-bottom: 5px;
        }
        
        .motor-speed {
            color: white;
            font-size: 24px;
            font-weight: bold;
        }
        
        .direction-display {
            color: white;
            font-size: 16px;
            font-weight: bold;
            margin-top: 15px;
            padding: 10px 20px;
            background: rgba(255, 255, 255, 0.2);
            border-radius: 8px;
        }
        
        /* Right side - Auxiliary Controls */
        .aux-section {
            flex: 0.6;
            display: flex;
            flex-direction: column;
            gap: 15px;
        }
        
        .aux-title {
            color: white;
            font-size: 20px;
            font-weight: bold;
            text-align: center;
            text-shadow: 2px 2px 4px rgba(0,0,0,0.3);
        }
        
        .aux-controls {
            flex: 1;
            display: flex;
            flex-direction: column;
            gap: 15px;
            justify-content: center;
        }
        
        .aux-card {
            background: rgba(255, 255, 255, 0.1);
            border-radius: 20px;
            backdrop-filter: blur(10px);
            padding: 25px;
            display: flex;
            align-items: center;
            justify-content: space-between;
        }
        
        .aux-info {
            display: flex;
            align-items: center;
            gap: 15px;
        }
        
        .aux-icon {
            font-size: 40px;
        }
        
        .aux-details {
            color: white;
        }
        
        .aux-name {
            font-size: 20px;
            font-weight: bold;
            margin-bottom: 5px;
        }
        
        .aux-status {
            font-size: 14px;
            opacity: 0.8;
        }
        
        /* Circular Toggle Button */
        .toggle-button {
            width: 80px;
            height: 80px;
            border-radius: 50%;
            border: none;
            cursor: pointer;
            transition: all 0.3s;
            box-shadow: 0 4px 15px rgba(0,0,0,0.3);
            font-size: 32px;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        
        .toggle-button.off {
            background: linear-gradient(145deg, #e74c3c, #c0392b);
        }
        
        .toggle-button.on {
            background: linear-gradient(145deg, #2ecc71, #27ae60);
        }
        
        .toggle-button:active {
            transform: scale(0.95);
        }
        
        .emergency-stop {
            width: 100%;
            padding: 15px;
            border: none;
            border-radius: 12px;
            background: linear-gradient(145deg, #e74c3c, #c0392b);
            color: white;
            font-size: 18px;
            font-weight: bold;
            cursor: pointer;
            box-shadow: 0 4px 15px rgba(0,0,0,0.3);
        }
        
        .emergency-stop:active {
            transform: scale(0.98);
        }
        
        @media (max-width: 768px) {
            .landscape-container {
                flex-direction: column;
            }
            
            .joystick-container {
                width: 220px;
                height: 220px;
            }
            
            .joystick-stick {
                width: 70px;
                height: 70px;
            }
        }
    </style>
</head>
<body>
    <div class="landscape-container">
        <!-- Left Side: Joystick Control -->
        <div class="joystick-section">
            <div class="joystick-title">🎮 MOBILITY CONTROL</div>
            
            <div class="joystick-container" id="joystick">
                <div class="joystick-center"></div>
                <div class="joystick-stick" id="stick"></div>
            </div>
            
            <div class="motor-info">
                <div class="motor-display">
                    <div class="motor-label">LEFT</div>
                    <div class="motor-speed" id="speedLeft">0%</div>
                </div>
                <div class="motor-display">
                    <div class="motor-label">RIGHT</div>
                    <div class="motor-speed" id="speedRight">0%</div>
                </div>
            </div>
            
            <div class="direction-display" id="direction">STOPPED</div>
        </div>
        
        <!-- Right Side: Auxiliary Controls -->
        <div class="aux-section">
            <div class="aux-title">⚙️ AUXILIARY</div>
            
            <div class="aux-controls">
                <!-- Pump Control -->
                <div class="aux-card">
                    <div class="aux-info">
                        <div class="aux-icon">💧</div>
                        <div class="aux-details">
                            <div class="aux-name">Pump</div>
                            <div class="aux-status" id="pumpStatus">OFF</div>
                        </div>
                    </div>
                    <button class="toggle-button off" id="pumpBtn" onclick="togglePump()">
                        ⏻
                    </button>
                </div>
                
                <!-- Aux Motor Control -->
                <div class="aux-card">
                    <div class="aux-info">
                        <div class="aux-icon">⚙️</div>
                        <div class="aux-details">
                            <div class="aux-name">Aux Motor</div>
                            <div class="aux-status" id="auxStatus">OFF</div>
                        </div>
                    </div>
                    <button class="toggle-button off" id="auxBtn" onclick="toggleAux()">
                        ⏻
                    </button>
                </div>
            </div>
            
            <button class="emergency-stop" onclick="emergencyStop()">
                ⚠️ EMERGENCY STOP
            </button>
        </div>
    </div>

    <script>
        // Joystick variables
        const container = document.getElementById('joystick');
        const stick = document.getElementById('stick');
        const directionText = document.getElementById('direction');
        const speedLeftText = document.getElementById('speedLeft');
        const speedRightText = document.getElementById('speedRight');
        
        let dragging = false;
        let centerX = 0;
        let centerY = 0;
        let maxDistance = 0;
        let lastSentX = 999;
        let lastSentY = 999;
        
        // Auxiliary states
        let pumpOn = false;
        let auxOn = false;
        
        function updateDimensions() {
            const rect = container.getBoundingClientRect();
            centerX = rect.width / 2;
            centerY = rect.height / 2;
            maxDistance = centerX - 45;
        }
        
        function sendPosition(x, y) {
            if (x === lastSentX && y === lastSentY) return;
            lastSentX = x;
            lastSentY = y;
            fetch(`/control?x=${x}&y=${y}`).catch(() => {});
        }
        
        function getDirection(x, y) {
            const threshold = 15;
            if (Math.abs(x) < threshold && Math.abs(y) < threshold) return 'STOPPED';
            if (Math.abs(y) > Math.abs(x) * 2) return y > 0 ? 'FORWARD' : 'REVERSE';
            if (Math.abs(x) > Math.abs(y) * 2) return x > 0 ? 'TURN RIGHT' : 'TURN LEFT';
            if (y > 0 && x > 0) return 'FORWARD RIGHT';
            if (y > 0 && x < 0) return 'FORWARD LEFT';
            if (y < 0 && x > 0) return 'REVERSE RIGHT';
            if (y < 0 && x < 0) return 'REVERSE LEFT';
            return 'STOPPED';
        }
        
        function updateUI(x, y) {
            let leftSpeed = y + x;
            let rightSpeed = y - x;
            leftSpeed = Math.max(-100, Math.min(100, leftSpeed));
            rightSpeed = Math.max(-100, Math.min(100, rightSpeed));
            
            speedLeftText.textContent = Math.abs(Math.round(leftSpeed)) + '%';
            speedRightText.textContent = Math.abs(Math.round(rightSpeed)) + '%';
            directionText.textContent = getDirection(x, y);
        }
        
        function getClientPos(e) {
            if (e.touches) {
                return { x: e.touches[0].clientX, y: e.touches[0].clientY };
            }
            return { x: e.clientX, y: e.clientY };
        }
        
        function startDrag(e) {
            dragging = true;
            e.preventDefault();
        }
        
        function drag(e) {
            if (!dragging) return;
            e.preventDefault();
            
            const rect = container.getBoundingClientRect();
            const pos = getClientPos(e);
            let offsetX = pos.x - rect.left - centerX;
            let offsetY = pos.y - rect.top - centerY;
            
            const distance = Math.sqrt(offsetX * offsetX + offsetY * offsetY);
            if (distance > maxDistance) {
                const angle = Math.atan2(offsetY, offsetX);
                offsetX = Math.cos(angle) * maxDistance;
                offsetY = Math.sin(angle) * maxDistance;
            }
            
            stick.style.transform = `translate(calc(-50% + ${offsetX}px), calc(-50% + ${offsetY}px))`;
            
            const x = Math.round((offsetX / maxDistance) * 100);
            const y = Math.round(-(offsetY / maxDistance) * 100);
            
            updateUI(x, y);
            sendPosition(x, y);
        }
        
        function stopDrag() {
            if (!dragging) return;
            dragging = false;
            stick.style.transform = 'translate(-50%, -50%)';
            updateUI(0, 0);
            sendPosition(0, 0);
        }
        
        function emergencyStop() {
            dragging = false;
            stick.style.transform = 'translate(-50%, -50%)';
            updateUI(0, 0);
            lastSentX = 999;
            lastSentY = 999;
            fetch('/stop').catch(() => {});
        }
        
        // Auxiliary control functions
        function togglePump() {
            pumpOn = !pumpOn;
            fetch(`/pump/${pumpOn ? 'on' : 'off'}`)
                .then(() => updateAuxStatus())
                .catch(() => {});
        }
        
        function toggleAux() {
            auxOn = !auxOn;
            fetch(`/aux/${auxOn ? 'on' : 'off'}`)
                .then(() => updateAuxStatus())
                .catch(() => {});
        }
        
        function updateAuxStatus() {
            fetch('/auxstatus')
                .then(response => response.json())
                .then(data => {
                    // Update pump
                    pumpOn = data.pump;
                    const pumpBtn = document.getElementById('pumpBtn');
                    const pumpStatus = document.getElementById('pumpStatus');
                    pumpBtn.className = pumpOn ? 'toggle-button on' : 'toggle-button off';
                    pumpStatus.textContent = pumpOn ? 'ON' : 'OFF';
                    
                    // Update aux motor
                    auxOn = data.aux;
                    const auxBtn = document.getElementById('auxBtn');
                    const auxStatus = document.getElementById('auxStatus');
                    auxBtn.className = auxOn ? 'toggle-button on' : 'toggle-button off';
                    auxStatus.textContent = auxOn ? 'ON' : 'OFF';
                })
                .catch(() => {});
        }
        
        // Initialize
        updateDimensions();
        window.addEventListener('resize', updateDimensions);
        
        // Joystick events
        stick.addEventListener('mousedown', startDrag);
        document.addEventListener('mousemove', drag);
        document.addEventListener('mouseup', stopDrag);
        stick.addEventListener('touchstart', startDrag, { passive: false });
        document.addEventListener('touchmove', drag, { passive: false });
        document.addEventListener('touchend', stopDrag);
        
        // Auto-update auxiliary status
        setInterval(updateAuxStatus, 2000);
        updateAuxStatus();
    </script>
</body>
</html>
)rawliteral";
    
    server.send(200, "text/html", html);
}

// Handle mobility control
void handleControl() {
    if (!server.hasArg("x") || !server.hasArg("y")) {
        server.send(400, "text/plain", "Missing parameters");
        return;
    }
    
    int x = server.arg("x").toInt();
    int y = server.arg("y").toInt();
    
    x = constrain(x, -100, 100);
    y = constrain(y, -100, 100);
    
    if (x == 0 && y == 0) {
        stopAllMotors();
        server.send(200, "text/plain", "OK");
        return;
    }
    
    int leftSpeed = y + x;
    int rightSpeed = y - x;
    
    leftSpeed = constrain(leftSpeed, -100, 100);
    rightSpeed = constrain(rightSpeed, -100, 100);
    
    setMotorSpeed(1, leftSpeed);
    setMotorSpeed(2, rightSpeed);
    
    server.send(200, "text/plain", "OK");
}

// Handle stop
void handleStop() {
    stopAllMotors();
    server.send(200, "text/plain", "STOPPED");
}

// Handle pump ON
void handlePumpOn() {
    digitalWrite(PUMP_PIN, HIGH);
    pumpState = true;
    Serial.println("Pump turned ON");
    server.send(200, "text/plain", "Pump ON");
}

// Handle pump OFF
void handlePumpOff() {
    digitalWrite(PUMP_PIN, LOW);
    pumpState = false;
    Serial.println("Pump turned OFF");
    server.send(200, "text/plain", "Pump OFF");
}

// Handle aux motor ON
void handleAuxOn() {
    digitalWrite(AUX_MOTOR_PIN, HIGH);
    auxMotorState = true;
    Serial.println("Aux Motor turned ON");
    server.send(200, "text/plain", "Aux Motor ON");
}

// Handle aux motor OFF
void handleAuxOff() {
    digitalWrite(AUX_MOTOR_PIN, LOW);
    auxMotorState = false;
    Serial.println("Aux Motor turned OFF");
    server.send(200, "text/plain", "Aux Motor OFF");
}

// Handle auxiliary status
void handleAuxStatus() {
    String json = "{\"pump\":";
    json += pumpState ? "true" : "false";
    json += ",\"aux\":";
    json += auxMotorState ? "true" : "false";
    json += "}";
    server.send(200, "application/json", json);
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== ESP32 Unified Tank Controller ===");
    
    // Initialize mobility motor pins
    pinMode(R_EN1_PIN, OUTPUT);
    pinMode(L_EN1_PIN, OUTPUT);
    pinMode(R_EN2_PIN, OUTPUT);
    pinMode(L_EN2_PIN, OUTPUT);
    
    digitalWrite(R_EN1_PIN, LOW);
    digitalWrite(L_EN1_PIN, LOW);
    digitalWrite(R_EN2_PIN, LOW);
    digitalWrite(L_EN2_PIN, LOW);
    
    // Setup PWM for mobility motors
    ledcAttach(RPWM1_PIN, PWM_FREQ, PWM_RESOLUTION);
    ledcAttach(LPWM1_PIN, PWM_FREQ, PWM_RESOLUTION);
    ledcAttach(RPWM2_PIN, PWM_FREQ, PWM_RESOLUTION);
    ledcAttach(LPWM2_PIN, PWM_FREQ, PWM_RESOLUTION);
    
    ledcWrite(RPWM1_PIN, 0);
    ledcWrite(LPWM1_PIN, 0);
    ledcWrite(RPWM2_PIN, 0);
    ledcWrite(LPWM2_PIN, 0);
    
    // Initialize auxiliary device pins
    pinMode(PUMP_PIN, OUTPUT);
    pinMode(AUX_MOTOR_PIN, OUTPUT);
    digitalWrite(PUMP_PIN, LOW);
    digitalWrite(AUX_MOTOR_PIN, LOW);
    
    Serial.println("All pins initialized");
    
    // Connect to WiFi
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }
    
    Serial.println();
    Serial.println("WiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    // Setup web server routes
    server.on("/", handleRoot);
    server.on("/control", handleControl);
    server.on("/stop", handleStop);
    server.on("/pump/on", handlePumpOn);
    server.on("/pump/off", handlePumpOff);
    server.on("/aux/on", handleAuxOn);
    server.on("/aux/off", handleAuxOff);
    server.on("/auxstatus", handleAuxStatus);
    
    server.begin();
    Serial.println("Web server started");
    Serial.println("===========================================\n");
}

void loop() {
    server.handleClient();
    updateMotorRamp(1);
    updateMotorRamp(2);
}
