#include "pizzeria.h"

#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

#include "helper.h"


static void* cozinhar(void* arg);
static void* entregar(void* arg);

static const int TAM_MESA = 4;

static bool g_pizzaria_fechada;

static sem_t g_balcao_espaco;
static pizza_t* g_balcao_pizza;

static pthread_mutex_t g_pa;

static sem_t g_forno_espacos;

static pthread_mutex_t g_mesas;
static pthread_cond_t g_mesas_alguem_saiu;
static int g_mesas_livres;
static int g_mesas_total;

static sem_t g_garcons;
static pthread_attr_t g_pedido_independente;

static queue_t g_deck;

static pthread_t* g_pizzaiolos;
static int g_pizzaiolos_size;


// INFRAESTRUTURA

void pizzeria_init(int tam_forno, int n_pizzaiolos, int n_mesas,
                   int n_garcons, int tam_deck, int n_grupos) {
	g_pizzaria_fechada = false;

	sem_init(&g_balcao_espaco, false, 1);

	pthread_mutex_init(&g_pa, NULL);

	sem_init(&g_forno_espacos, false, tam_forno);

	g_mesas_total = n_mesas;
	g_mesas_livres = n_mesas;
	pthread_mutex_init(&g_mesas, NULL);
	pthread_cond_init(&g_mesas_alguem_saiu, NULL);

	sem_init(&g_garcons, false, n_garcons);
	pthread_attr_init(&g_pedido_independente);
	pthread_attr_setdetachstate(&g_pedido_independente, PTHREAD_CREATE_DETACHED);

	queue_init(&g_deck, tam_deck);

	// @NOTE: n_grupos nao utilizado

	g_pizzaiolos = malloc(sizeof(pthread_t) * n_pizzaiolos);
	assert(g_pizzaiolos != NULL);
	g_pizzaiolos_size = n_pizzaiolos;
	for (int i = 0; i < g_pizzaiolos_size; ++i)
		pthread_create(&g_pizzaiolos[i], NULL, cozinhar, NULL);
}

void pizzeria_close(void) {
	// fecha a entrada
	g_pizzaria_fechada = true;

	// espera os clientes sairem
	pthread_mutex_lock(&g_mesas);
	while (g_mesas_livres < g_mesas_total)
		pthread_cond_wait(&g_mesas_alguem_saiu, &g_mesas);
	pthread_mutex_unlock(&g_mesas);

	// libera os pizzaiolos
	for (int i = 0; i < g_pizzaiolos_size; ++i)
		queue_push_back(&g_deck, NULL);
	for (int i = 0; i < g_pizzaiolos_size; ++i)
		pthread_join(g_pizzaiolos[i], NULL);
}

void pizzeria_destroy(void) {
	free(g_pizzaiolos);

	queue_destroy(&g_deck);

	pthread_attr_destroy(&g_pedido_independente);
	sem_destroy(&g_garcons);

	pthread_cond_destroy(&g_mesas_alguem_saiu);
	pthread_mutex_destroy(&g_mesas);

	sem_destroy(&g_forno_espacos);

	pthread_mutex_destroy(&g_pa);

	sem_destroy(&g_balcao_espaco);
}


// FUNCIONARIOS
// Comportamento de cada um dos pizzaiolos.
static void* cozinhar(void* arg) {
	while (true) {
		// pega um pedido do smartdeck
		pedido_t* pedido = queue_wait(&g_deck);
		// check para finalizacao das threads pizzaiolos
		if (g_pizzaria_fechada && pedido == NULL)
			break;

		// monta o pedido
		pizza_t* pizza = pizzaiolo_montar_pizza(pedido);
		sem_init(&(pizza->pronta), false, 0);

		// espera um espaco no forno
		sem_wait(&g_forno_espacos);
		// usa a pa para colocar a pizza no forno
		pthread_mutex_lock(&g_pa);
		pizzaiolo_colocar_forno(pizza);
		pthread_mutex_unlock(&g_pa);

		// espera ate que a pizza esteja pronta
		sem_wait(&(pizza->pronta));

		// espera espaco no balcao
		sem_wait(&g_balcao_espaco);
		// usa a pa para tirar a pizza do forno (liberando espaco)
		pthread_mutex_lock(&g_pa);
		pizzaiolo_retirar_forno(pizza);
		sem_post(&g_forno_espacos);
		pthread_mutex_unlock(&g_pa);

		// finalizando o preparo
		sem_destroy(&(pizza->pronta));

		// coloca um pegador junto a pizza no balcao
		g_balcao_pizza = pizza;
		// @FIXME: esse mutex nunca eh des-inicializado
		pthread_mutex_init(&(g_balcao_pizza->pegador), NULL);
		// avisa a equipe de garcons
		pthread_t garcons_equipe;
		pthread_create(&garcons_equipe, &g_pedido_independente, entregar, NULL);
	}
	return NULL;
}

// Chamada a equipe de garcons para entregar um pedido.
static void* entregar(void* arg) {
	// espera ate que um garcom esteja livre
	sem_wait(&g_garcons);

	// pega a pizza e libera espaco do balcao
	pizza_t* pizza = g_balcao_pizza;
	sem_post(&g_balcao_espaco);

	// entrega a pizza e desocupa o garcom
	garcom_entregar(pizza);
	sem_post(&g_garcons);
	return NULL;
}

void pizza_assada(pizza_t* pizza) {
	// indica que a pizza esta pronta
	sem_post(&(pizza->pronta));
}


// CLIENTES

int pegar_mesas(int tam_grupo) {
	// espera ate conseguir sentar todos do grupo
	const int mesas_necessarias = tam_grupo / TAM_MESA + (tam_grupo % TAM_MESA != 0);
	pthread_mutex_lock(&g_mesas);
	while (mesas_necessarias > g_mesas_livres) {
		// so confere denovo quando alguem sair
		pthread_cond_wait(&g_mesas_alguem_saiu, &g_mesas);
	}

	// grupo se senta se a pizzaria ainda estiver aberta
	if (!g_pizzaria_fechada) {
		g_mesas_livres -= mesas_necessarias;
		pthread_mutex_unlock(&g_mesas);
		return 0;
	} else {
		// pizaria fechada -> desistem
		pthread_mutex_unlock(&g_mesas);
		return -1;
	}
}

void fazer_pedido(pedido_t* pedido) {
	// manda o pedido para o smartdeck
	queue_push_back(&g_deck, pedido);
}

int pizza_pegar_fatia(pizza_t* pizza) {
	// espera o pegador
	pthread_mutex_lock(&(pizza->pegador));
	// se ainda houver fatia
	if (pizza->fatias >= 1) {
		// come e solta o pegador
		pizza->fatias--;
		pthread_mutex_unlock(&(pizza->pegador));
		return 0;
	} else {
		// solta o pegador e para de comer
		pthread_mutex_unlock(&(pizza->pegador));
		return -1;
	}
}

void garcom_chamar(void) {
	// chama um garcom para a mesa
	sem_wait(&g_garcons);
}

void garcom_tchau(int tam_grupo) {
	// depois de pagar, libera o garcom que chamou
	sem_post(&g_garcons);

	// desocupa as mesas em que o grupo estava
	const int mesas_ocupadas = tam_grupo / TAM_MESA + (tam_grupo % TAM_MESA != 0);
	pthread_mutex_lock(&g_mesas);
	g_mesas_livres += mesas_ocupadas;
	pthread_mutex_unlock(&g_mesas);
	pthread_cond_broadcast(&g_mesas_alguem_saiu);
}
