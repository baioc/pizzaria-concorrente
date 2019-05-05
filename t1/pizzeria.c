#include "pizzeria.h"

#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

#include "helper.h"

static void* cozinhar(void* arg);


const int TAM_MESA = 4;

static bool pizzaria_fechada;
static pthread_mutex_t pa;
static sem_t forno_espacos;
static pthread_mutex_t mesas;
static int mesas_livres;
static sem_t garcons;
static queue_t deck;
static pthread_t* pizzaiolo;
static int pizzaiolo_size;


// RESTAURANTE

void pizzeria_init(int tam_forno, int n_pizzaiolos, int n_mesas,
                   int n_garcons, int tam_deck, int n_grupos) {
	assert(tam_forno > 0);
	assert(n_pizzaiolos > 0);
	assert(n_mesas > 0);
	assert(n_garcons > 0);
	assert(tam_deck > 0);
	assert(n_grupos > 0);

	pizzaria_fechada = false;

	// @TODO n_grupos ?

	pthread_mutex_init(&pa, NULL);

	sem_init(&forno_espacos, false, tam_forno);

	mesas_livres = n_mesas;
	pthread_mutex_init(&mesas, NULL);

	sem_init(&garcons, false, n_garcons);

	queue_init(&deck, tam_deck);

	pizzaiolo = malloc(sizeof(pthread_t) * n_pizzaiolos);
	assert(pizzaiolo != NULL);
	pizzaiolo_size = n_pizzaiolos;
	for (int i = 0; i < pizzaiolo_size; ++i)
		pthread_create(&pizzaiolo[i], NULL, cozinhar, NULL);
}

void pizzeria_close(void) {
	pizzaria_fechada = true;

	for (int i = 0; i < pizzaiolo_size; ++i)
		pthread_join(pizzaiolo[i], NULL);
}

void pizzeria_destroy(void) {
	free(pizzaiolo);

	queue_destroy(&deck);

	sem_destroy(&garcons);

	pthread_mutex_destroy(&mesas);

	sem_destroy(&forno_espacos);

	pthread_mutex_destroy(&pa);
}


// PIZZAIOLOS

static void* cozinhar(void* arg) {
	while (!pizzaria_fechada || !queue_empty(&deck)) {
		pedido_t* pedido = queue_wait(&deck); // @FIXME possivel deadlock

		pizza_t* pizza = pizzaiolo_montar_pizza(pedido);
		sem_init(&(pizza->pronta), false, 0);

		sem_wait(&forno_espacos);
		pthread_mutex_lock(&pa);
		pizzaiolo_colocar_forno(pizza);
		pthread_mutex_unlock(&pa);

		sem_wait(&(pizza->pronta));

		pthread_mutex_lock(&pa);
		pizzaiolo_retirar_forno(pizza);
		pthread_mutex_unlock(&pa);
		sem_post(&forno_espacos);

		sem_destroy(&(pizza->pronta));

		// @TODO: uninitialize pizza->pegador
		pthread_mutex_init(&(pizza->pegador), NULL);

		// @TODO: balcao ?
		garcom_entregar(pizza);
	}
	return NULL;
}

void pizza_assada(pizza_t* pizza) {
	sem_post(&(pizza->pronta));
}


// CLIENTES

int pegar_mesas(int tam_grupo) {
	int mesas_necessarias = tam_grupo / TAM_MESA + (tam_grupo % TAM_MESA != 0);
	while (mesas_necessarias > 0) {
		if (!pizzaria_fechada) {
			pthread_mutex_lock(&mesas);
			if (mesas_necessarias <= mesas_livres) {
				mesas_livres -= mesas_necessarias;
				mesas_necessarias = 0;
			}
			pthread_mutex_unlock(&mesas);
		} else {
			return -1;
		}
	}
	return 0;
}

void fazer_pedido(pedido_t* pedido) {
	queue_push_back(&deck, pedido);
}

int pizza_pegar_fatia(pizza_t* pizza) {
	pthread_mutex_lock(&(pizza->pegador));
	if (pizza->fatias >= 1) {
		pizza->fatias--;
		pthread_mutex_unlock(&(pizza->pegador));
		return 0;
	} else {
		pthread_mutex_unlock(&(pizza->pegador));
		return -1;
	}
}

void garcom_chamar(void) {
	sem_wait(&garcons);
}

void garcom_tchau(int tam_grupo) {
	sem_post(&garcons);

	const int mesas_ocupadas = tam_grupo / TAM_MESA + (tam_grupo % TAM_MESA != 0);
	pthread_mutex_lock(&mesas);
	mesas_livres += mesas_ocupadas;
	pthread_mutex_unlock(&mesas);
}
