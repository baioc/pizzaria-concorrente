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

static bool pizzaria_fechada;

static sem_t balcao_espaco;
static pizza_t* balcao_pizza;

static pthread_mutex_t pa;

static sem_t forno_espacos;

static pthread_mutex_t mesas;
static int mesas_livres;
static int mesas_total;

static sem_t garcons;
static pthread_attr_t independente;

static queue_t deck;

static sem_t grupos_concorrentes;

static pthread_t* pizzaiolos;
static int pizzaiolos_size;


// INFRAESTRUTURA

void pizzeria_init(int tam_forno, int n_pizzaiolos, int n_mesas,
                   int n_garcons, int tam_deck, int n_grupos) {
	assert(tam_forno > 0);
	assert(n_pizzaiolos > 0);
	assert(n_mesas > 0);
	assert(n_garcons > 0);
	assert(tam_deck > 0);
	assert(n_grupos > 0);

	pizzaria_fechada = false;

	sem_init(&balcao_espaco, false, 1);

	pthread_mutex_init(&pa, NULL);

	sem_init(&forno_espacos, false, tam_forno);

	mesas_total = n_mesas;
	mesas_livres = n_mesas;
	pthread_mutex_init(&mesas, NULL);

	sem_init(&garcons, false, n_garcons);
	pthread_attr_init(&independente);
	pthread_attr_setdetachstate(&independente, PTHREAD_CREATE_DETACHED);

	queue_init(&deck, tam_deck);

	sem_init(&grupos_concorrentes, false, n_grupos);

	pizzaiolos = malloc(sizeof(pthread_t) * n_pizzaiolos);
	assert(pizzaiolos != NULL);
	pizzaiolos_size = n_pizzaiolos;
	for (int i = 0; i < pizzaiolos_size; ++i)
		pthread_create(&pizzaiolos[i], NULL, cozinhar, NULL);
}

static bool pizzaria_vazia(void) {
	pthread_mutex_lock(&mesas);
	const int livres = mesas_livres;
	pthread_mutex_unlock(&mesas);
	return livres >= mesas_total;
}

void pizzeria_close(void) {
	// fecha a entrada
	pizzaria_fechada = true;

	// espera os clientes sairem
	while(!pizzaria_vazia());

	// @TODO: espera os pizzaiolos sairem
	// for (int i = 0; i < pizzaiolos_size; ++i)
	// 	pthread_join(pizzaiolos[i], NULL);
}

void pizzeria_destroy(void) {
	free(pizzaiolos);

	sem_destroy(&grupos_concorrentes);

	queue_destroy(&deck);

	pthread_attr_destroy(&independente);
	sem_destroy(&garcons);

	pthread_mutex_destroy(&mesas);

	sem_destroy(&forno_espacos);

	pthread_mutex_destroy(&pa);

	sem_destroy(&balcao_espaco);
}


// FUNCIONARIOS
// Comportamento de cada um dos pizzaiolos.
static void* cozinhar(void* arg) {
	while (true) {
		// pega um pedido do smartdeck
		pedido_t* pedido = queue_wait(&deck);

		// monta o pedido
		pizza_t* pizza = pizzaiolo_montar_pizza(pedido);
		sem_init(&(pizza->pronta), false, 0);

		// espera um espaco no forno
		sem_wait(&forno_espacos);
		// usa a pa para colocar a pizza no forno
		pthread_mutex_lock(&pa);
		pizzaiolo_colocar_forno(pizza);
		pthread_mutex_unlock(&pa);

		// espera ate que a pizza esteja pronta
		sem_wait(&(pizza->pronta));

		// espera espaco no balcao
		sem_wait(&balcao_espaco);
		// usa a pa para tirar a pizza do forno (liberando espaco)
		pthread_mutex_lock(&pa);
		pizzaiolo_retirar_forno(pizza);
		sem_post(&forno_espacos);
		pthread_mutex_unlock(&pa);

		// finalizando o preparo
		sem_destroy(&(pizza->pronta));

		// coloca um pegador junto a pizza no balcao
		balcao_pizza = pizza;
		// @FIXME: esse mutex nunca eh des-inicializado
		pthread_mutex_init(&(balcao_pizza->pegador), NULL);
		// avisa a equipe de garcons
		pthread_t garcons_equipe;
		pthread_create(&garcons_equipe, &independente, entregar, NULL);
	}
	return NULL;
}

// Chamada a equipe de garcons para entregar um pedido.
static void* entregar(void* arg) {
	// espera ate que um garcom esteja livre
	sem_wait(&garcons);

	// pega a pizza e libera espaco do balcao
	pizza_t* pizza = balcao_pizza;
	sem_post(&balcao_espaco);

	// entrega a pizza
	garcom_entregar(pizza);

	// desocupa este garcom
	sem_post(&garcons);
	return NULL;
}

void pizza_assada(pizza_t* pizza) {
	// indica que a pizza esta pronta
	sem_post(&(pizza->pronta));
}


// CLIENTES

int pegar_mesas(int tam_grupo) {
	// limita o numero de grupos tentando sentar
	sem_wait(&grupos_concorrentes);

	// enquanto o grupo nao estiver sentado
	int mesas_necessarias = tam_grupo / TAM_MESA + (tam_grupo % TAM_MESA != 0);
	while (mesas_necessarias > 0) {
		// se a pizzaria estiver aberta
		if (!pizzaria_fechada) {
			// tenta sentar ocupando as mesas necessarias
			pthread_mutex_lock(&mesas);
			if (mesas_necessarias <= mesas_livres) {
				mesas_livres -= mesas_necessarias;
				mesas_necessarias = 0;
			}
			pthread_mutex_unlock(&mesas);
		} else {
			// pizaria fechada -> desistem
			sem_post(&grupos_concorrentes);
			return -1;
		}
	}

	// libera a entrada de um futuro grupo
	sem_post(&grupos_concorrentes);
	return 0;
}

void fazer_pedido(pedido_t* pedido) {
	// manda o pedido para o smartdeck
	queue_push_back(&deck, pedido);
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
	sem_wait(&garcons);
}

void garcom_tchau(int tam_grupo) {
	// depois de pagar, libera o garcom que chamou
	sem_post(&garcons);

	// desocupa as mesas em que o grupo estava
	const int mesas_ocupadas = tam_grupo / TAM_MESA + (tam_grupo % TAM_MESA != 0);
	pthread_mutex_lock(&mesas);
	mesas_livres += mesas_ocupadas;
	pthread_mutex_unlock(&mesas);
}
