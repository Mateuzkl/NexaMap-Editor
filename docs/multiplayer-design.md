# Multiplayer: estudo do MME e adaptação ao NexaMap

Base de trabalho: `codex/nexamap-layout-redesign`. O MME local é somente referência.

## Comparação antes da implementação

O MME usa `LiveServer` (acceptor Asio), `LivePeer` (conexão de cada participante),
`LiveClient`, `LiveSocket` (codec), `NetworkedActionQueue` e `NetworkedBatchAction`.
O handshake compara versão do editor e do protocolo, negocia o client Tibia e
envia dimensões, posição inicial e metadados. A visualização consulta folhas da
QTree de 4x4 tiles, com máscara de andares. Tiles usam nós OTBM. `DirtyList` agrega
tiles/folhas alterados; o host aplica `ACTION_REMOTE` e retransmite aos interessados,
excluindo o autor. Undo/redo também gera dirty tiles. Cursores, chat, towns,
World Palette, locks, pings, status e pedidos de aprovação têm pacotes próprios.
Heartbeat mede tempo de ida e volta; a reconexão abre uma nova conexão e editor.

O NexaMap possui `Editor`, `ActionQueue`, `Action`, `BatchAction`, `Change`, `Map`,
`QTreeNode`, `Tile`, `IOMapOTBM`, `MainMenuBar` e `MapDrawer`, mas não os módulos
Live do MME. Sua fila já tem transações de paste de houses, zonas e rollback de
lotes. Seu formato inclui ClientID/ServerID, OTBM 5, variantes de spawn, atributos
de criaturas e zonas que o codec Live do MME não preserva. Substituir esses
arquivos pelos do MME perderia recursos existentes.

### Problemas confirmados no código de referência

* `LivePeer::parseHello` lê a senha, mas não a compara com a senha da sessão.
* `receive` de client/peer redimensiona o buffer pelo tamanho recebido sem limite.
* `LiveServer::acceptClient` remove conexões anteriores com o mesmo IP.
* `LiveServer::close` chama `peer->close()` e `delete peer`; `close` já chega a
  `removeClient`, que remove a entrada e apaga o peer. Há também invalidação da
  iteração sobre `clients`.
* `parseReceiveChanges` aplica tiles sem revisão, transação, permissão ou
  validação final de lock. A ordem de chegada decide o resultado.
* O peer possui uma flag de lifetime para alguns callbacks de GUI, mas callbacks
  Asio ainda capturam `this`. O client usa `CallAfter` e thread de reconnect
  destacada capturando `this`.
* Há filas de escrita, porém sem limite de bytes/pacotes pendentes.
* Os handlers `parseAddHouse`, `parseEditHouse` e `parseRemoveHouse` estão vazios.

Essas observações descrevem o checkout examinado, não garantias sobre outras
versões do MME.

### Origem do código

O README do MME declara GPLv2/custom, os cabeçalhos Live declaram GPLv3-or-later
e LICENSE.rtf contém uma EULA histórica. Por essa inconsistência, esta adaptação
é uma implementação nova, orientada pelos conceitos e pelas interfaces locais
do NexaMap; não transplanta os arquivos Live nem remove créditos existentes.

## Arquitetura escolhida

* `multiplayer_protocol`: protocolo binário limitado, leitura estrita, framing,
  revisões, identidades, locks e journal; independente da GUI para testes.
* `multiplayer_crypto`: SHA-256, PBKDF2, HMAC-SHA256 e números aleatórios de
  bibliotecas do sistema (CNG no Windows, OpenSSL nas outras plataformas).
* `multiplayer_codec`: serialização do estado completo de cada tile e dos
  metadados próprios do NexaMap. IDs internos seguem o codec do editor, sem
  conversão implícita entre ClientID e ServerID.
* `multiplayer_session`: host autoritativo, conexões, transações, snapshot,
  reconexão, permissões, aprovação e histórico de undo da sessão.
* `multiplayer_window`: Host/Join, participantes, chat, aprovações e diagnóstico.

O NexaMap já depende de **wxWidgets net**, mas não de Boost.Asio. A rede usa
sockets wx assíncronos/não bloqueantes com eventos na thread da GUI, escrita
parcial enfileirada e orçamento de processamento por evento. Assim, nenhuma
thread de rede toca `Map`, `Tile`, `ActionQueue` ou widgets, não há threads
destacadas e não se adiciona Boost somente para networking.

O host atribui revisão crescente a cada transação aceita. Alterações em tiles
diferentes podem coexistir sobre revisão antiga; interseção com alteração mais
nova causa rejeição explícita e atualização do cliente. O servidor valida papel,
sessão, transação, limites, locks, conteúdo e IDs antes de alterar o mapa.
O autor recebe confirmação autoritativa; alterações remotas não entram no undo
local. Undo/redo envia outra transação com precondições, em vez de rebobinar
o histórico de outro participante.

Cada conexão tem fila limitada e dono único. A identidade de reconexão é um token
aleatório emitido pelo host; gerações distinguem reconexões. Senha não é enviada
nem salva em texto. Autenticação e integridade estão descritas na seção seguinte.

O snapshot inicial percorre a QTree e envia lotes de tiles com sequência/revisão,
sem criar arquivos OTBM temporários nem serializar o mapa a cada alteração.
Um journal limitado permite recuperar o intervalo de revisões e reconectar;
quando ele não contém o intervalo completo, inicia-se outro snapshot.
Compressão e interesse por viewport são otimizações futuras que precisam de
medição, não condições para a consistência do protocolo.

## Segurança da sessão (protocolo 2)

**Escopo: somente LAN confiável ou VPN.** Não exponha a porta diretamente à
Internet. O protocolo autentica os participantes e cada frame, mas **não cifra
nada**: mapa, chat, nomes e tokens de reconexão trafegam legíveis. Quem captura um
login (sniffer) ou se passa pelo host pode testar senhas offline contra a prova
HMAC, limitado apenas pelo custo do PBKDF2 (100 mil iterações). Só uma senha longa
e única reduz esse risco; TLS-PSK ou um PAKE continuam sendo o caminho para
qualquer uso fora de LAN/VPN.

Hospedar exige senha de pelo menos 8 caracteres, exceto na opção *somente este PC*,
que escuta apenas em 127.0.0.1 e aceita senha vazia.

**Handshake.** O host deriva `K = PBKDF2-HMAC-SHA256(senha, sessão, 100000)` uma
única vez em `host()` e descarta a senha, de modo que cada login custa um HMAC, sem
PBKDF2 alcançável antes da autenticação. O cliente deriva a mesma chave e a guarda
enquanto a sessão não muda.

1. `Challenge`: versão, ID da sessão e nonce de 32 bytes do host.
2. `Hello`: corpo com versões, assinatura de assets, nonce de 32 bytes do cliente,
   token, geração, base de resume e nome, seguido da prova do cliente.
3. `Welcome`: primeiro frame com MAC; traz o token e a prova do host.

Com `T = SHA-256(sessão, nonce do host, corpo do Hello)`, cada valor é
`HMAC(K, rótulo || T)` com rótulo próprio: prova do cliente, prova do host, chave
cliente→host e chave host→cliente. O nonce do cliente impede repetir uma prova de
host gravada. Até a prova do host ser verificada, o cliente não altera sessão,
histórico nem ID local.

**Integridade.** Depois do `Welcome`, todo frame termina com
`HMAC(chave da direção, sequência || tipo || conteúdo)`. A sequência de 64 bits não
vai no fio: replay, reordenação, remoção ou frame injetado falham no MAC, e a
conexão é encerrada antes de o conteúdo ser processado. A verificação ocorre na
leitura, também para frames adiados. Um relay ainda obtém um socket autenticado,
mas sem as chaves ele não consegue enviar nenhum frame válido. O MAC não impede
derrubar a conexão TCP.

Antes da autenticação não há MAC: `Disconnect` vindo do host só é exibido se for
uma recusa conhecida, e o host registra tentativas pré-autenticação apenas como
contagem periódica.

**Limites antes da autenticação.** Prazo absoluto de 15 s desde o accept; 32
handshakes pendentes no total e 4 por IP, fora da contagem de jogadores; espera
exponencial por IP após senha errada (1 s a 60 s); 32 tentativas de login a cada
10 s no total. Esse limite global também pode atrasar logins legítimos durante uma
inundação vinda de muitos IPs.

**Identidade.** O host emite o token no `Welcome`, e o papel fica ligado a ele.
Como a senha é compartilhada, nada impede alguém de descartar o token e entrar como
novo participante; por isso o rebaixamento também limita o papel de identidades
novas do mesmo IP, e kick bane o token e o IP até o host usar **Clear bans**. Trocar
de IP contorna essas duas medidas: elas limitam abuso casual, não substituem contas
individuais. Atrás de NAT/VPN com IP compartilhado, afetam todos desse IP. A tabela
de identidades descarta as mais antigas desconectadas ao chegar a 1024 entradas.

**Limites de edição.** Raio de spawn em transações: até 150, o máximo padrão da
paleta e das propriedades (`MAX_SPAWN_RADIUS`). O XML pode carregar até 255: esses
spawns sincronizam por snapshot e continuam editáveis, mas nenhuma edição cria um.
Spawns novos ou com raio alterado somam até 4 Mi de células por transação, verificado
antes do commit; o cliente aceita 256 Mi de células e 128 Mi de tiles vindos do host.
Editores não criam tiles além de 32 posições após as dimensões do mapa. Tiles
decodificados não alocam nada no mapa até a validação terminar.

Locks: até 512×512 tiles por lease e 32 leases por dono. O host ignora locks de
clientes e pode quebrá-los. Unique IDs novos exigem aprovação, e `uidRefCount`
acompanha commit e undo. Snapshot não rejeita duplicatas, pois elas já existem no
mapa do host; editar um tile com duplicata antiga é permitido, criar outra não. Os
metadados usam a mesma validação de posição ao codificar e decodificar, e a
aprovação mostra o que muda.

## Ordem de implementação e verificação

1. Protocolo, limites, identidades, revisão/conflito, leases e testes isolados.
2. Codec do NexaMap e validação completa antes de commit.
3. Host/join, autenticação, framing, fila e encerramento.
4. Snapshot, transações e integração com `ActionQueue`/undo.
5. Metadados, aprovação, papéis, cursores, chat e pings.
6. Reconexão/resync, diagnóstico e roteiro de teste em PCs separados.

Integrações com maior risco: commit/rollback de ações já aplicadas; metadados
editados por diálogos fora da fila; alterações de versão/assets com sessão
aberta; fechamento de aba com eventos de socket pendentes. Ferramentas que
reescrevem o mapa fora de transações devem exigir encerrar a sessão.

O teste manual prolongado com host e dois clientes, corte de conexão, save/reopen
e assets reais é necessário antes de declarar estabilidade. Compilar ou passar
testes isolados não comprova essa estabilidade.

## Corre��es de conex�o e lifetime

A porta padr�o � 49171 e Join come�a sem endere�o. A interface diferencia
loopback, LAN e VPN, lista IPv4 de interfaces locais e copia o endpoint sem
consultar um servi�o web. DNS e conex�o t�m prazo; os estados e motivos finais
s�o registrados no diagn�stico.

Cada socket tem um ID de evento pr�prio. Eventos antigos precisam coincidir com
o socket e esse ID, evitando aceitar um evento de uma conex�o anterior quando
um endere�o de mem�ria for reutilizado. Rejei��es transferem a propriedade do
socket para um handler independente, com prazo de dois segundos. Ele completa
um eventual frame parcial, escreve Disconnect e envia FIN, sem sleep, sem manter
refer�ncias ao Peer/Session e sem apagar a sess�o dentro de um callback.

O fechamento da aba destr�i a sess�o, timers e janela na thread da GUI, antes de
liberar o mapa em background. Guards de opera��es usam refer�ncias fracas; lotes
de a��es possuem um token de lifetime. Transfer�ncias deixam de acessar a frente
da fila depois que um limite provoca drop. Altera��es de participantes/locks
causadas por drop s�o publicadas pelo timer, evitando broadcast recursivo.

O timer compara timestamps antes de subtrair valores sem sinal: um socket criado
durante o tick n�o pode ganhar um timeout imediato por underflow.

Um snapshot interrompido invalida a base para resume. Uma reconex�o s� usa o
journal quando houve sincroniza��o completa; caso contr�rio recebe novo snapshot.
Pap�is persistem no token de reconex�o emitido pelo host e a contagem m�xima tamb�m
vale para uma identidade conhecida que retorna ap�s sair.

Propriedades aguardam o lock por eventos e revalidam posi��o/sele��o antes do
di�logo; callbacks guardam janela/sess�o por refer�ncia fraca. Locks s�o liberados
ao cancelar/terminar ou ap�s a confirma��o/rejei��o de uma transa��o pendente.

Ver [roteiro e testes de rede](tests/multiplayer-network.md).
