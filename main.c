/* ========================================
 *
 * Copyright YOUR COMPANY, THE YEAR
 * All Rights Reserved
 * UNPUBLISHED, LICENSED SOFTWARE.
 *
 * CONFIDENTIAL AND PROPRIETARY INFORMATION
 * WHICH IS THE PROPERTY OF your company.
 *
 * ========================================
*/

#include "project.h"

#define and &&
#define or ||

#define variable Variables.Variable
#define caso Casos.Caso

#define sobrepeso (medida.peso/100 == 38 and medida.peso%100 > 55) or (medida.peso/100 > 38)
#define hipertermia (medida.temperatura/10 == 70 and medida.temperatura%10 >= 3) or (medida.temperatura/10 > 70)
#define hipotermia (medida.temperatura/10 == 8 and medida.temperatura%10 >= 2) or (medida.temperatura/10 > 8)


uint8 claveOriginal[4] = {1,4,3,2};                                     // Vectores para acceder al sistema
uint8 claveIngresada[4] = {0,0,0,0};

uint8 Led[8] = {0x1, 0x3, 0x7, 0xF, 0x1F, 0x3F, 0x7F, 0xFF};            // Se crea un vector con los valores que 
uint8 led;                                                              // encenderan los leds uno por uno cada 100ms
    
char *mensaje = "Error, intente de nuevo ";                             // Mensaje mostrado para clave incorrecta
char *vacio = " ";

                                                                        // ------------------------------------------
typedef struct Tiempo{                                                  // Estructura con las variables de tiempo
    uint64 ms:10;                                                       
    uint64 ms_350:9;
    uint64 seg:6;
    uint64 periodo:10;
}Tiempo;
Tiempo tiempo;

typedef struct Medidas{                                                 // Estructura con las variables de sensado y su rango
    uint64 peso:13;                                                     // [0 : 5000]
    uint64 temperatura:11;                                              // [0 : 1100]
    uint64 acumuladoTemp:17;                                            // [0 : 81900]
    uint64 acumuladoPeso:17;                                            // [0 : 81900]
    uint64 duty:10;
}medidas;
medidas medida;

typedef union Banderas_1{                                               // Unión entre una variable de reseteo
    struct Variables{                                                   // y variables generales para el control
        uint16 botonPresionado:2;                                       // del sistema
        uint16 posicion:3;
        uint16 estado:3;
        uint16 contador:5;                                              // Contador empleado en titilar y sensar (max = 20)
        uint16 signo:1;
        uint16 salirPresionado:1;
        uint16 PWM_dormido:1;
    }Variable;
    uint16 resetear;
}banderas_1;
banderas_1 Variables;

typedef union Banderas_2{                                               // Unión entre una variable de reseteo
    struct Casos{                                                       // y variables para controlar los rangos
        uint8 fueraRango:2;                                             // de seguridad del sistema
        uint8 sobrePeso:1;
        uint8 hipoTermia:1;
        uint8 hiperTermia:1;
    }Caso;
    uint8 resetear;
}banderas_2;
banderas_2 Casos;
                                                                        // ------------------------------------------
void verDisplay(void);                                                  // Funciones para manejo del display
void titilar(float frecuencia);
void restablecerBrillo(void);
void apagarBrillo(void);

void verLCD(void);

void ingresarClave(void);                                               // Rutinas de ejecución de las tareas
void compararClave(const uint8 *ingresada , const uint8 *original);     // del sistema
void sensar(void);          
void analizarRangos(void);
void salir(void);
                                                                        
CY_ISR_PROTO(contador_ms);                                              // Funciones de las interrupciones
CY_ISR_PROTO(botonIngreso);
CY_ISR_PROTO(botonConfirmar);
CY_ISR_PROTO(apagarAlarma);
CY_ISR_PROTO(botonSalir);

int main(void)
{
    CyGlobalIntEnable; 
    isr_botonIngreso_StartEx(botonIngreso);                             // Inicialización de interrupciones
    isr_botonConfirmar_StartEx(botonConfirmar);
    isr_apagarAlarma_StartEx(apagarAlarma);
    isr_botonSalir_StartEx(botonSalir);
    isr_ms_StartEx(contador_ms);
                                                                     
    Contador_Start();                                                   // Inicialización de componentes
    Displays_Start();
    LCD_Start();
    ADC_Start();
    AMux_Start();
    
    LCD_LoadCustomFonts(LCD_customFonts);                               // Estado inicial de componentes
    LCD_ClearDisplay();                                                 
    Displays_Write7SegNumberDec(0,0,4,Displays_ZERO_PAD);
    
    variable.estado = 3;
    for(;;)
    {                                              
        switch (variable.estado){
            case 0:                                                     // ESTADO 0:    
                titilar(1.5);                                           // En el estado inicial los displays
                break;                                                  // titilan a 1.5 Hz
                
            case 1:                                                     // ESTADO 1:
                ingresarClave();                                        // El usuario ingresa su clave mientras 1
                titilar(1.5);                                           // display titila a 1.5 Hz
                
                if (tiempo.seg == 40) {
                    for (uint8 posicion = 0; posicion <4; posicion++) claveIngresada[posicion] = 0;
                    verDisplay();                                       // Si pasados 40 segundos no se ha confirmado
                    Variables.resetear = 0;                             // la clave, el sistema vuelve al estado
                    tiempo.seg = 0;                                     // inicial
                }
                break;
                
            case 2:                                                     // ESTADO 2:
                compararClave(claveIngresada,claveOriginal);            // El sistema compara la clave ingresada
                break;                                                  // con la clave de acceso
                
            case 3:                                                     // ESTADO 3:
                PWM_WritePeriod(1000);                                  // Estado de sensado constante y validación
                PWM_WriteCompare(1000);                                    // de los rangos de seguridad
                PWM_Start();
                while (variable.estado == 3){                           // El ciclo while evita que se
                    if (tiempo.ms%25 == 0) {                            // salten periodos de sensado
                        sensar();
                    }                                                   // Se llama la función de sensado cada 25ms
                    if (tiempo.ms%500 == 0) {                           // para tomar 20 muestras en medio segundo, y
                        verLCD();                                       // el promedio se imprime en pantalla cada
                        analizarRangos();                               // medio segundo, así se evita saturar el LCD
                    }                                                   
                    if (variable.salirPresionado){                      // Sí se presiona el botonSalir1 vez, la variable seg se
                        if (botonSalir_Read() == 0) {                   // reinicia a 0, mientras que si se deja presionado y
                            variable.salirPresionado = 0;               // pasan 4 seg, el estado del sistema cambia a 4
                            tiempo.seg = 0;
                        }
                        if (tiempo.seg == 4 and botonSalir_Read() == 1) variable.estado = 4;
                    }                                                   
                }
                break;
            
            case 4:                                                     // ESTADO 4:
                salir();                                                // Salir del sistema de medición
                break;
            
        }
        
    }
}

                                                                        // Funciones de tareas del sistema
void salir(void){
    PWM_Stop();                                                         // Se detiene el pwm de la alarma sonora,
    LCD_ClearDisplay();                                                 // el sensado y la visualización en pantalla
    LCD_WriteControl(LCD_CURSOR_HOME);
    LCD_Position(0,0);
    LCD_PrintString("Saliendo del sistema");
    
    variable.posicion = 0;
    tiempo.periodo = 0;
    while (led != 0b11111111){
        if (tiempo.periodo == 100){
            tiempo.periodo = 0;
            led = Led[variable.posicion];
            leds_Write(led);
            variable.posicion++;
        }
    }
    CyDelay(200);
      
    led = 0;
    leds_Write(led);                                                      // Se retorna el sistema a su estado inicial
    LCD_ClearDisplay();
    Variables.resetear = 0; 
    Casos.resetear = 0;
    for (uint8 posicion = 0; posicion < 4; posicion++) claveIngresada[posicion] = 0;
    variable.estado = 0;
    Displays_Write7SegNumberDec(0,0,4,Displays_ZERO_PAD);
}

void analizarRangos(void){
    Casos.resetear = 0;                                                 // Resetea la union que contiene las variables
                                                                        // que controlan los rangos seguros del sistema
    if (sobrepeso){
        caso.sobrePeso = 1;
        caso.fueraRango++;                                              // Inicialmente el sistema analiza si las 
    }                                                                   // las variables se encuentran en su rango seguro,
    if (variable.signo == 0 and (hipertermia)) {                        // de lo contrario, se activan banderas dentro
        caso.hiperTermia = 1;                                           // de la estructura "caso" para diferenciar CUANTAS
        caso.fueraRango++;                                              // y CUALES variables se salieron de su rango
    }
    if (variable.signo == 1 and (hipotermia) ) {
        caso.hipoTermia = 1;
        caso.fueraRango++;
    }
    
    
    if(caso.fueraRango == 0) {                                          // En caso de que ninguna variable esté fuera
        LCD_Position(0,0);LCD_PrintString("Sistema de medicion");       // de su rango seguro, no se activa ninguna
        LCD_Position(3,9);LCD_PutChar(LCD_CUSTOM_3);                    // advertencia, ni ninguna alarma
        
        if (variable.PWM_dormido) PWM_Start();
        
        medida.duty = 1000;
        PWM_WritePeriod(medida.duty); 
        PWM_WriteCompare(0);
    }
        
    else if(caso.fueraRango == 1){                                      // Sí solo 1 variable esta fuera de rango,
        LCD_Position(3,9);LCD_PutChar(LCD_CUSTOM_4);                    // se analiza cual de todas es mediante las
        LCD_Position(0,0);                                              // banderas sobrepeso, hipotermia, hipertermia,
                                                                        // y se activa una alarma sonora a una frecuencia
        if (variable.PWM_dormido and medida.duty == 500) PWM_Start();   // particular según sea el caso (3Hz o 1Hz)
        
        if (caso.sobrePeso){                                            // Además, se imprime en pantalla, una acción
            LCD_PrintString("Alerta debe ");                            // sugerida para devolver el sistema a su estado seguro
            LCD_PutChar(LCD_CUSTOM_2);
            LCD_PrintString("Kg");
            
            medida.duty = 999;
            PWM_WritePeriod(medida.duty);
            PWM_WriteCompare(medida.duty/2);
        }
        else if (caso.hipoTermia){
            LCD_PrintString("Alerta debe ");
            LCD_PutChar(LCD_CUSTOM_1);
            LCD_PrintString("T");
            
            medida.duty = 332;
            PWM_WritePeriod(medida.duty);
            PWM_WriteCompare(medida.duty/2);
        }
        else if (caso.hiperTermia){
            LCD_PrintString("Alerta debe ");
            LCD_PutChar(LCD_CUSTOM_2);
            LCD_PrintString("T");
            
            medida.duty = 332;
            PWM_WritePeriod(medida.duty);
            PWM_WriteCompare(medida.duty/2);
        }
    }
        
    else if (caso.fueraRango == 2){                                     // En caso de que 2 variables esten fuera de 
        LCD_Position(3,9);LCD_PutChar(LCD_CUSTOM_4);                    // su rango seguro, se activa una alarma
        LCD_Position(0,0);                                              // continua, y se imprimen en pantalla, las
                                                                        // acciones sugeridas para regresar el sistema
        LCD_PrintString("Alerta debe ");                                // a su estado seguro
        LCD_PutChar(LCD_CUSTOM_2);
        LCD_PrintString("Kg y ");
        
        if (variable.PWM_dormido and (medida.duty == 999 or medida.duty == 332)) PWM_Start();
        medida.duty = 500;
        PWM_WritePeriod(medida.duty);                                   // Nota: En caso de que las alarmas sonoras                               
        PWM_WriteCompare(medida.duty);                                  // se desactiven mediante el botón externo
                                                                        // se activa una bandera PWM_dormido, la cual
        if (caso.hiperTermia){                                          // se usa como referencia para reactivar las
            LCD_PutChar(LCD_CUSTOM_2);                                  // alarmas en caso de que LA CANTIDAD de variables
            LCD_PrintString("T");                                       // fuera de rango CAMBIE
        }
        
        else if (caso.hipoTermia){
            LCD_PutChar(LCD_CUSTOM_1);
            LCD_PrintString("T");
        }
    }
    
}

void sensar(void){
    variable.contador++;                                                // Esta variable cuenta las veces que se sensan las variables
                                                                        
    AMux_Select(0);                                                     // Rutina de sensado para el peso
    ADC_StartConvert();                                                         
    ADC_IsEndConversion(ADC_WAIT_FOR_RESULT);
    medida.acumuladoPeso += ADC_GetResult16();                          // bits Max = 4095*20 = 81900 
    
    AMux_Select(1);                                                     // Rutina de sensado para la temperatura
    ADC_StartConvert();
    ADC_IsEndConversion(ADC_WAIT_FOR_RESULT);
    medida.acumuladoTemp += ADC_GetResult16();                          // bits Max = 4095*20 = 81900                          
    
    if (variable.contador == 20){                                       // Cuando se ha sensado 20 veces
        variable.contador = 0;                                          // se hace un promedio de los 
        medida.acumuladoPeso/=20;                                       // registros
        medida.acumuladoTemp/=20;
        LCD_Position(0,0);
        LCD_PrintNumber(medida.acumuladoPeso);
        CyDelay(500);
        
//        LCD_Position(1,16);LCD_PrintNumber(medida.acumuladoPeso);       // Estas lineas imprimen el promedio
//        LCD_Position(2,16);LCD_PrintNumber(medida.acumuladoTemp);       // en bits, para validar la información
//        LCD_Position(3,16);LCD_PrintString("Bits");
                                                                        // CONDICIONALES PARA EL PESO
        
        if (medida.acumuladoPeso <= 3890.25){
            medida.peso = 100*medida.acumuladoPeso*50/3890.25;
        }
        else medida.peso = 100*50;
        
                                                                        // CONDICIONALES PARA LA TEMPERATURA
        
        if (medida.acumuladoTemp >= 1228.5 and medida.acumuladoTemp <= 3931.2) {
            medida.temperatura = (uint32)10*(medida.acumuladoTemp - 958.23)*100/2702.7;
            variable.signo = 0;                                         // Imprime valor positivo
        }
        else if (medida.acumuladoTemp > 3931.2) medida.temperatura = 110*10; 
        else{
            if (medida.acumuladoTemp >= 819){
                medida.temperatura = (uint32)10*(medida.acumuladoTemp - 819)*30/1228.5;
                variable.signo = 0;                                     // Imprime valor positivo
            }
            else{
                medida.temperatura = (uint32)10*(819 - medida.acumuladoTemp)*30/1228.5;
                variable.signo = 1;                                     // Imprime valor negativo
            }
        }
        
        medida.acumuladoPeso = 0;
        medida.acumuladoTemp = 0;
    }
    
}

void compararClave(const uint8 *ingresada , const uint8 *original){
    uint8 aciertos = 0;                                                 // Variables internas para el control del
    uint8 i = 0;                                                        // mensaje impreso en el LCD
    uint8 j = 2;
    uint8 velocidad;
    
    for (uint8 i = 0; i < 4; i++){     
        if (*ingresada == *original) aciertos++;                        // Cuenta los digitos correctos
        ingresada++;                                                    // Aumenta la posicion del digito a validar 
        original++;                                                     // en los vectores clave ingresada y original
    } 
    
    if (aciertos == 4){                                                 // Si los aciertos fueron 4, se escribe una H
        Displays_WriteString7Seg("k   ",0);                             // en los displays, y titila a 2Hz por 3 segundos
        tiempo.seg = 0;                                                 // antes de que el sistema pase al estado siguiente
        variable.posicion = 0;      
        while (tiempo.seg < 4) titilar(2);
        Displays_ClearDisplayAll();
        variable.estado = 3;
    }
    
    else{
        Displays_Write7SegDigitHex(12,0);                               // Si no se obtuvieron 4 aciertos, se 
        Displays_WriteString7Seg("   ",1);                              // escribe una C (12 en hexadecimal= y se 
                                                                        // muestre un mensaje barrido en el LCD
        for (uint8 posicion = 0; posicion < 4; posicion++) claveIngresada[posicion] = 0;
        
        LCD_ClearDisplay();
        LCD_Position(0,19);
        variable.posicion = 0;
        tiempo.seg = 0;
        tiempo.ms = 0;
        tiempo.periodo = 0;
        
        velocidad = 50;                                                 // Para mostrar el mensaje barrido, se 
        while (tiempo.seg < 11) {                                       // contaron las acciones a realizar (43)
            titilar(1);                                                 // y se distribuyeron en los 10 segundos
                                                                        // que debe durar el barrido del mensaje
            if (tiempo.ms%250 == 0){  
                titilar(1);
                if (i < 19){
                    LCD_PutChar(*mensaje);                              // Este condicional imprime y barre caracteres
                    CyDelay(velocidad);                                 // hasta que llegan al límite izquierdo de la 
                    LCD_WriteControl(LCD_DISPLAY_SCRL_LEFT);            // pantalla
                    mensaje++;
                    i++;
                }
                
                else if (i == 19){
                    LCD_PutChar(*mensaje);                              // Los condicionales correspondientes a 
                    CyDelay(velocidad);                                 // i = 19, 20, 21 y 22; completan los 
                    LCD_Position(0,19);LCD_PutChar(*vacio);             // 4 caracteres faltantes del mensaje
                    LCD_WriteControl(LCD_DISPLAY_SCRL_LEFT);                    
                    mensaje++;
                    i++;
                }
                
                else if (i == 20){
                    LCD_Position(2,19);LCD_PutChar(*mensaje);           // Estos 4 condicionales se hicieron                 
                    CyDelay(velocidad);                                 // de forma individual, dado que el comando
                    LCD_Position(2,0);LCD_PutChar(*vacio);              // SCRL (scroll) tambien mueve la posición
                    LCD_WriteControl(LCD_DISPLAY_SCRL_LEFT);            // del cursor, haciendo perder de vista la
                    mensaje++;                                          // ubicación actual
                    i++;
                }                                                       // pos ?? -> e
                    
                else if (i == 21){
                    LCD_Position(0,0);LCD_PutChar(*mensaje);            // pos 0,0 -> v
                    CyDelay(velocidad);
                    LCD_Position(2,1);LCD_PutChar(*vacio);
                    LCD_WriteControl(LCD_DISPLAY_SCRL_LEFT);
                    mensaje++;
                    i++;
                }
                else if(i == 22){
                    LCD_Position(0,1);LCD_PutChar(*mensaje);            // pos 2,2 -> o
                    i++;
                }
                
                else if (i > 22 and i < 41){                            // En este rango condicional, únicamente
                    LCD_Position(2,j);LCD_PutChar(*vacio);              // se borra la letra del limite izquierdo
                    CyDelay(velocidad);                                 // de la pantalla y se desplaza el mensaje
                    LCD_WriteControl(LCD_DISPLAY_SCRL_LEFT);            // a la izquierda
                    i++;
                    j++;                                                // j ubica el caracter vacio en el lim izquierdo
                }                                                       // a medida que se desplaza el cursor
                    
                else if (i == 41){                                      // Luego de esto, restan 2 letras por borrar,
                    LCD_Position(0,0);LCD_PutChar(*vacio);              // lo cual se hace de forma individual
                    CyDelay(velocidad);
                    LCD_WriteControl(LCD_DISPLAY_SCRL_LEFT);
                    i++;
                }
                else if (i == 42){
                    LCD_Position(0,1);LCD_PutChar(*vacio);
                    CyDelay(velocidad);
                    LCD_WriteControl(LCD_DISPLAY_SCRL_LEFT);
                    i++;
                }
            }
            titilar(1);
        }
        for (uint8 i = 0; i < 22; i++) mensaje--;                       // Finalmente, el apuntador empleado para
        LCD_ClearDisplay();                                             // imprimir los caracteres del mensaje
        LCD_WriteControl(LCD_CURSOR_HOME);                              // se vuelve a apuntar a la posición inicial
                                                                        // y el cursor se retorna a la posición de inicio
        restablecerBrillo();
        Variables.resetear = 0;
        verDisplay();                                                   // Esta función muestra la letra 
    }                                                                   // correspondiente al numero de aciertos
}

void ingresarClave(void){
    if (variable.botonPresionado == 2 && tiempo.ms_350 < 350){          // Si se presiona el boton de ingreso 2 veces
        variable.botonPresionado = 0;                                   // en mnos de 350 ms, la variable posición
        tiempo.ms_350 = 0;                                              // aumenta, y en caso de que dicha variable
        variable.posicion++;                                            // llegue a 4, se reinicia a 0
        restablecerBrillo();
        if (variable.posicion == 4)variable.posicion = 0;                
    }

    if (variable.botonPresionado == 1 && tiempo.ms_350 >= 350){         // Si transcurridos 350ms el boton de ingreso
        variable.botonPresionado = 0;                                   // sólo se ha presionado 1 vez, se analiza la
        tiempo.ms_350 = 0;                                              // posición del display y se incrementa su valor
        claveIngresada[variable.posicion]++;                            // actual, sin embargo, si llega a ser 10, se
                                                                        // reinicia a 0
        if (claveIngresada[variable.posicion] == 10) claveIngresada[variable.posicion] = 0;
        verDisplay();                                                   // Sólo si hubo cambios en el display, 
    }                                                                   // se llama la función para visualizarlos
    
    
}
                                                                        // Funciones para control del Display
void verDisplay(void){                                                  // Se imprime en los displays según 
    if (variable.estado < 3){                                           // el estado del sistema
        Displays_Write7SegDigitDec(claveIngresada[0],0);
        Displays_Write7SegDigitDec(claveIngresada[1],1);
        Displays_Write7SegDigitDec(claveIngresada[2],2);
        Displays_Write7SegDigitDec(claveIngresada[3],3);
    }
}

void titilar (float frecuencia){      
    uint16 periodo_ms;
    periodo_ms = (1/frecuencia)*1000;                                   // Se calcula el período de la señal
                                                                        
    if (tiempo.periodo == periodo_ms/2){                                // Cada que los milisegundos sean iguales
        tiempo.periodo = 0;                                             // a la mitad del periodo de la señal, se 
        if (variable.contador == 0) variable.contador = 1;              // cambia el estado del brillo del display
        else if (variable.contador == 1) variable.contador = 0;
    
                                                                        // Si el estado del sistema es 0,
        if (variable.estado == 0){                                      // titilan todos los displays
            if (variable.contador == 0) apagarBrillo();
            if (variable.contador == 1) restablecerBrillo();
        }
        
        else{                                                           // Si el estado del sistema es otro, titila
                                                                        // solo el display de la posicion actual
            if (variable.contador == 0) Displays_SetBrightness(0,variable.posicion);    
            else if (variable.contador == 1) Displays_SetBrightness(255,variable.posicion);
        }
    }
}

void restablecerBrillo(void){                                           // Pone alto el brillo de los displays
    for (uint8 posicion_display = 0; posicion_display < 4; posicion_display++)
        {
            Displays_SetBrightness(255,posicion_display);
        }
}

void apagarBrillo(void){                                                // Pone bajo el brillo de los displays
    for (uint8 posicion_display = 0; posicion_display < 4; posicion_display++)
        {
            Displays_SetBrightness(0,posicion_display);
        }
}
                                                                        // Funciones para control del LCD
void verLCD(void){
    LCD_ClearDisplay();                                                 // Se muestran las variables de sensado
    LCD_Position(1,13);LCD_PrintString("Kg");                           
    LCD_Position(2,13);LCD_PutChar(LCD_CUSTOM_0);LCD_PrintString("F");
//    LCD_Position(3,16);LCD_PrintString("Bits");
    
    LCD_Position(1,0);LCD_PrintString("Peso = ");                       // Rutina de impresión del peso
    LCD_PrintNumber(medida.peso/100);
    LCD_PutChar('.');
    if (medida.peso%100 < 10) LCD_PrintNumber(0);
    LCD_PrintNumber(medida.peso%100);
    
    LCD_Position(2,0);                                                  // Rutina de impresión de la temperatura
    switch (variable.signo){
        case 0:
            LCD_PrintString("Temp = ");
            break;
        case 1:
            LCD_PrintString("Temp = -");
            break;
    }
    
    LCD_PrintNumber(medida.temperatura/10); 
    LCD_PutChar('.');
    LCD_PrintNumber(medida.temperatura%10);
}
                                                                        // Funciones de interrupcion de los botones y el contador
CY_ISR(botonConfirmar){
    if (variable.estado < 2) variable.estado = 2;
}

CY_ISR(botonIngreso){                                                   // Si el sistema está en el estado 0,
    if (variable.estado == 0) {                                         // se aumenta la variable boton presionado
        variable.estado = 1;                                            // y el estado del sistema pasa a ser 1.
        restablecerBrillo();                                            // Además, se reinicia el conteo de los 350ms
    }
    variable.botonPresionado++;                                         
    tiempo.ms_350 = 0;
}

CY_ISR(apagarAlarma){
    if (variable.estado == 3){
        variable.PWM_dormido = 1;
        PWM_Stop();
    }
}

CY_ISR(botonSalir){
    variable.salirPresionado = 0;
    if (variable.estado == 3) {
        tiempo.seg = 0;
        tiempo.ms = 0;
        variable.salirPresionado = 1;
    }
}

CY_ISR(contador_ms){                                                    // La interrupción del contador 
    tiempo.ms++;                                                        // incrementa las variables dentro de la
    if (variable.estado < 2) tiempo.ms_350++;                                                    // estructura tiempo
    tiempo.periodo++;
    if (tiempo.ms == 1000) {
        tiempo.ms = 0;
        tiempo.seg++;
        if (tiempo.seg == 61) tiempo.seg = 0;
    }
}





/* [] END OF FILE */
