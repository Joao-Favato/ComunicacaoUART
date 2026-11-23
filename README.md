## Controle de LEDs pela UART

No primeiro commit, fizemos o código solicitado de controle de LEDs pela UART, com o protocolo fixo de 6 bytes e todas as ações solicitadas funcionando. Nessa versão, os LEDs atuam somente em dois estados, aceso ou apagado, sem controle de intensidade. Além disso, o checksum simples é calculado por XOR, e não por CRC-8.

Evidências do funcionamento do programa podem ser vistos nos vídeos da pasta evidências, em que estávamos mandando as informações manualmente pela porta serial a fim de mostrar o comportamento adequado. Não colocamos o vídeo diretamente no README pois o arquivo ficou muito grande, e também não faria sentido colocá-los como gifs pois seria perdida toda a resolução necessária para ver as mensagens no terminal e nossas entradas manuais.
