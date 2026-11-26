## Controle de LEDs pela UART

No primeiro commit, fizemos o código solicitado de controle de LEDs pela UART, com o protocolo fixo de 6 bytes e todas as ações solicitadas funcionando. Nessa versão, os LEDs atuam somente em dois estados, aceso ou apagado, sem controle de intensidade. Além disso, o checksum simples é calculado por XOR, e não por CRC-8. Para verificar o funcionamento do código, realizamos os dois testes detalhados a seguir. 

**Evidências do funcionamento do programa podem ser vistos nos vídeos da pasta "*evidencias*", em que estávamos mandando as informações manualmente pela porta serial a fim de mostrar o comportamento adequado. Não colocamos o vídeo diretamente no README pois o arquivo ficou muito grande, e também não faria sentido colocá-los como gifs pois seria perdida toda a resolução necessária para ver as mensagens no terminal e nossas entradas manuais.**

### Teste do Controle dos LEDs
Pré-condição: A placa foi ligada e reiniciada pelo botão de “reset”, para garantir que
não houvesse nenhum lixo de memória.

Etapas:
1. Conectou-se a placa ao computador pelo cabo USB;
2. Abriu-se o monitor serial na porta de comunicação da placa;
3. Mudou-se o modo de enviar mensagem para "Enviar como Hexa";
4. Foi enviado o pacote "01 FF 00 00 01 FF" (Ligar Vermelho);
5. Foi enviado o pacote "01 00 FF 00 01 FF" (Ligar Verde);
6. Foi enviado o pacote "01 FF 00 FF 04 05" (Piscar Roxo (1s)).

Pós-condição: Através dos testes, foi possível atestar o funcionamento do envio dos pacotes para controle dos LEDs. Além do funcionamento das cores separadas, também é possível verificar que outras ações, como o piscar, estão corretas. O vídeo de evidência do teste pode ser encontrado em *evidencias/TesteControleDosLeds.mp4*.

### Teste do Tratamento de Erro
Pré-condição: A placa foi ligada e reiniciada pelo botão de “reset”, para garantir que
não houvesse nenhum lixo de memória.

Etapas:
1. Conectou-se a placa ao computador pelo cabo USB;
2. Abriu-se o monitor serial na porta de comunicação da placa;
3. Mudou-se o modo de enviar mensagem para "Enviar como Hexa";
4. Foi enviado o pacote "02 FF 00 00 01 FC" (ID errado);
5. Foi enviado o pacote "01 FF 00 00 01 00" (Checksum inválido);
6. Foi enviado o pacote "01 FF 00" (Pacote truncado).

Pós-condição: Verificando as mensagens no terminal, foi possível afirmar que o tratamento de erro está funcionando corretamente no código. As mensagens enviadas pela porta serial indicam exatamente a causa de cada erro acontecer, facilitando a identificação dos problemas em fase de desenvolvimento. O vídeo de evidência do teste pode ser encontrado em *evidencias/TesteTratamentoDeErro.mp4*.


## Controle de LEDs por PWM e CRC-8

Nessa atualização do código, a principal mudança feita por adição do controle da intensidade do led por PWM, em vez de apenas os estados ligado e desligado da última iteração do código. Como o código anterior já suportava um número em hexadecimal de 0 a 255 para cada cor de LED, nos aproveitamos disso e essa escala virou a medida de intensidade (0 apagado / 255 luminosidade máxima). 

Para a escala citada funcionar, foi feita uma função de para converter a intensidade em cnv, permitindo o uso adequado da aplicação das luminosidades. A implementação do pwm foi feita com uso de uma biblioteca disponibilizada no curso de Microcontroladores I.

Além disso, também foi trocado o uso da confirmação por XOR pelo uso de CRC-8, que gera um checksum mais confiável. Sua implementação foi facilitada pela biblioteca nativa do zephyr. 

Nessa fase, foram feitos três testes: um para verificar que funções não relacionados ao PWM e CRC-8 ainda são funcionais, o teste para mostrar o funcionamento do controle de intensidade e outro teste para verificar o tratamento de erros já com a utilização do CRC-8

### Teste das Funções Anteriores
Pré-condição: A placa foi ligada e reiniciada pelo botão de “reset”, para garantir que
não houvesse nenhum lixo de memória.

Etapas:
1. Conectou-se a placa ao computador pelo cabo USB;
2. Abriu-se o monitor serial na porta de comunicação da placa;
3. Mudou-se o modo de enviar mensagem para "Enviar como Hexa";
4. Foi enviado o pacote "01 FF 00 00 01 B4" (Vermelho máximo);
5. Foi enviado o pacote "01 00 00 00 02 6C" (Desligar LEDs);
6. Foi enviado o pacote "01 00 00 00 03 6B" (Toggle).

Pós-condição: Através da utilização desses pacotes, verificou-se que funções que não dependiam do controle do PWM ou do CRC-8, como o Toggle, desligar os LEDs ou led ligado, continuam funcionando corretamente no novo versionamento. O vídeo de evidência do teste pode ser encontrado em *evidencias/TesteFuncoesAnteriores.mp4*.

### Teste do Controle de Intensidade com PWM
Pré-condição: A placa foi ligada e reiniciada pelo botão de “reset”, para garantir que
não houvesse nenhum lixo de memória.

Etapas:
1. Conectou-se a placa ao computador pelo cabo USB;
2. Abriu-se o monitor serial na porta de comunicação da placa;
3. Mudou-se o modo de enviar mensagem para "Enviar como Hexa";
4. Foi enviado o pacote "01 19 00 00 01 A4" (Vermelho fraco (~10%));
5. Foi enviado o pacote "01 FF 00 00 01 B4" (Vermelho máximo);

Pós-condição: Ativando o vermelho fraco (~10%) e o vermelho máximo logo em seguida, é mostrado claramente no vídeo a diferença de brilho, evidenciando o controle adequado por PWM. Tais testes também pode ser feitos com as outras três cores e o funcionamento continua sendo visível. O vídeo de evidência do teste pode ser encontrado em *evidencias/TesteControlePWM.mp4*.

### Teste de Tratamento de Erros com CRC-8
Pré-condição: A placa foi ligada e reiniciada pelo botão de “reset”, para garantir que
não houvesse nenhum lixo de memória.

Etapas:
1. Conectou-se a placa ao computador pelo cabo USB;
2. Abriu-se o monitor serial na porta de comunicação da placa;
3. Mudou-se o modo de enviar mensagem para "Enviar como Hexa";
4. Foi enviado o pacote "01 FF 00 00 01 FF" (CRC-8 inválido (ex.: pacote antigo em XOR));
5. Foi enviado o pacote "02 FF 00 00 01 12" (ID da placa incorreto (ID = 0x02 com CRC válido));
6. Foi enviado o pacote "01 FF 00" (Pacote truncado).

Pós-condição: Testando um pacote anterior com CHECK baseado em XOR, foi verificado que agora o código usa CRC-8, inclusive exibindo a mensagem de erro adequadamente. Além do funcionamento certo do CRC-8, também pode ser verificado que demais erros, como do ID incorreto e de pacotes truncados, continuam sendo tratados como esperado. O vídeo de evidência do teste pode ser encontrado em *evidencias/TesteTratamentoDeErroCRC-8.mp4*.